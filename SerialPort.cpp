#include "stdafx.h"
//#include "CommonLock.h"
#include "SerialPort.h"

namespace SerialPort
{
    template <typename ... Types>
    void ShowLog(int t, const Types& ... args)
    {
        //if (sizeof...(args) > 0) CConfig::Instance()->ShowLog(t, args...);
        if (sizeof...(args) > 0) printf(args...);
    }

    Package::Package(void* addr, DWORD size)
    {
        data = new BYTE[size];
        len = size;
        memcpy(data, addr, size);
    }

    Package::~Package()
    {
        if (data)
        {
            delete data;
            data = nullptr;
        }
        len = 0;
    }

    Package::Package(Package&& other)
    {
        data = other.data;
        len = other.len;

        other.data = nullptr;
        other.len = 0;
    }

    VOID WINAPI CompletedWriteRoutine(DWORD, DWORD, LPOVERLAPPED);
    VOID WINAPI CompletedReadRoutine(DWORD, DWORD, LPOVERLAPPED);

    CSerialPort::CSerialPort(CompletedReadCallback funcCompletedReadCallback)
        :m_ucom(INVALID_HANDLE_VALUE),
        m_hEvent(INVALID_HANDLE_VALUE),
        m_funcCompletedReadCallback(funcCompletedReadCallback)
    {
        m_oRead.Pointer = this;
        m_oWrite.Pointer = this;
        memset(m_readBuffer, 0, sizeof(m_readBuffer));
        m_readLength = 0;
    }


    CSerialPort::~CSerialPort()
    {
        Stop();
    }

    void CSerialPort::Push(void* addr, DWORD size)
    {
        m_wirteMutex.lock();
        m_writeQueue.emplace(Package(addr, size));
        if (m_writeQueue.size() == 1)
        {
            SetEvent(m_hEvent);
        }
        m_wirteMutex.unlock();
    }

    void CSerialPort::OnWriteComplete(DWORD dwWriteBytes)
    {
        ShowLog(3, "CSerialPort::OnWriteComplete %lu Bytes", dwWriteBytes);
        m_wirteMutex.lock();
        if (!m_writeQueue.empty())
        {
            m_writeQueue.pop();
        }
        if (m_writeQueue.size() > 0)
        {
            SetEvent(m_hEvent);
        }
        m_wirteMutex.unlock();
    }

    void CSerialPort::OnReadComplete(DWORD dwReadBytes)
    {
        ShowLog(3, "CSerialPort::OnReadComplete %lu Bytes", dwReadBytes);
        if (dwReadBytes > 0)
        {
            m_ref--;

            m_readLength += dwReadBytes;
            if (m_funcCompletedReadCallback)
            {
                m_funcCompletedReadCallback(m_readBuffer, m_readLength);
            }
            m_readLength = 0;
        }
        else
        {
            ShowLog(3, "OnReadComplete  error: %lu", GetLastError());

            if (m_ref > 0)
            {
                m_ref--;
            }
        }

        AsyncRead();
        AsyncWirte();
    }

    bool CSerialPort::Start(const char* comPort)
    {
        if (!m_thread.joinable())
        {
            m_comPort = comPort;
            m_thread = std::thread(&CSerialPort::Run, this);
            return true;
        }
        return false;
    }

    void CSerialPort::Stop()
    {
        m_end = true;
        SetEvent(m_hEvent);
        m_thread.join();
    }

    void CSerialPort::AsyncWirte()
    {
        if (m_ref > 0)
        {
            return;
        }

        m_wirteMutex.lock();
        if (!m_writeQueue.empty())
        {
            ShowLog(3, "AsyncWirte");
            Package& pkg = m_writeQueue.front();
            if (WritePort(pkg.data, pkg.len))
            {
                m_ref++;
            }
        }
        m_wirteMutex.unlock();
    }

    void CSerialPort::AsyncRead()
    {
        ShowLog(3, "AsyncRead");
        ReadPort(m_readBuffer, sizeof(m_readBuffer));
    }

    bool CSerialPort::OpenPort(const char* comPort)
    {
        if (m_ucom != INVALID_HANDLE_VALUE)
        {
            return true;
        }
        std::string com_port = "\\\\.\\";//这里可以根据实际COM进行设置,这里是用的COM8
        com_port.append(comPort);
        //CreateFileA对应来往数据采用ASCII编码格式，CreateFileW对应来往数据采用UNICODE编码格式
        m_ucom = CreateFileA(com_port.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
        if (m_ucom == INVALID_HANDLE_VALUE)
        {
            return false;

        }
        return true;
    }

    bool CSerialPort::InitPort(const char* comPort)
    {
        //打开串口
        if (!OpenPort(comPort))
        {
            ShowLog(3, "Open Serial Port Failed  error: %lu", GetLastError());
            return false;
        }

        DCB dcb;//结构体变量类型，串口设置信息规定使用数据类型，成员为串口连接设置选项，包括波特率,数据位长度等

        memset(&dcb, 0X0, sizeof(dcb));
        GetCommState(m_ucom, &dcb);

        dcb.BaudRate = 38400;  //波特率设置为38400
        dcb.ByteSize = 8;       //8位数据位            * 4-8可选
        dcb.StopBits = 0;       //停止位             * 0:1位停止位 1：1.5位停止位 2：2位停止位
        dcb.fParity = 0;        //不进行奇偶校验       * 0：不校验 1：校验
        dcb.fNull = 0;          //不允许空串           * 0：不允许 1：允许
        dcb.Parity = 0;         //不进行校验           * 0：不校验 1：奇校验 2：偶校验 3：mark 4:space

        //设置串口连接信息,读写缓存大小采用默认设置
        if (!SetCommState(m_ucom, &dcb))
        {
            ShowLog(3, "Set Serial port error: %lu", GetLastError());
            return false;
        }

        //设置读写缓存器大小，可选，不用表示采用默认设置
        if (!SetupComm(m_ucom, 1048576, 1048576))
        {
            ShowLog(3, "Set the Size of Serial Receive Buffer and Send Buffer Failed, Error: %lu", GetLastError());
            return false;
        }

        //超时处理,单位：毫秒
        //总超时＝时间系数×读或写的字符数＋时间常量
        COMMTIMEOUTS TimeOuts;
        TimeOuts.ReadIntervalTimeout = 10; //读间隔超时
        TimeOuts.ReadTotalTimeoutMultiplier = 50; //读时间系数
        TimeOuts.ReadTotalTimeoutConstant = 5000; //读时间常量
        TimeOuts.WriteTotalTimeoutMultiplier = 500; // 写时间系数
        TimeOuts.WriteTotalTimeoutConstant = 2000; //写时间常量
        SetCommTimeouts(m_ucom, &TimeOuts);

        // 清空错误
        COMSTAT comstat;
        DWORD dwErrFlags = 0;
        ClearCommError(m_ucom, &dwErrFlags, &comstat);

        //最后清空发送接收缓存区
        if (!PurgeComm(m_ucom, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR))
        {
            ShowLog(3, "Clear Receiver Buffer and Send Buffer Failed, Error: %lu", GetLastError());
            return false;
        }
        ShowLog(3, "Open Serial Port Succeed");
        return true;
    }

    void CSerialPort::InitEvent()
    {
        m_hEvent = CreateEvent(
            NULL,    // default security attribute
            FALSE,    // manual reset event 
            FALSE,    // initial state = signaled 
            NULL);   // unnamed event object 

        if (m_hEvent == NULL)
        {
            ShowLog(3, "CreateEvent failed, Error: %lu", GetLastError());
        }
    }

    bool CSerialPort::ReadPort(void* addr, DWORD size)
    {
        bool bResult = false;
        if (ReadFileEx(m_ucom, addr, size, &m_oRead, CompletedReadRoutine))
        {
            DWORD dwErrorCode = GetLastError();
            if (ERROR_SUCCESS != dwErrorCode)
            {
                SetError();
                ShowLog(3, "ReadFileEx Failed, Error: %lu", dwErrorCode);
                KERNEL_ASSERT(0);
            }
            else
            {
                bResult = true;
            }
        }
        else
        {
            SetError();
            ShowLog(3, "ReadFileEx Failed, Error: %lu", GetLastError());
            KERNEL_ASSERT(0);
        }

        return bResult;
    }

    bool CSerialPort::WritePort(void* addr, DWORD size)
    {
        bool bResult = false;
        if (WriteFileEx(m_ucom, addr, size, &m_oWrite, CompletedWriteRoutine))
        {
            DWORD dwErrorCode = GetLastError();
            if (ERROR_SUCCESS != dwErrorCode)
            {
                SetError();
                ShowLog(3, "WriteFileEx Failed, Error: %lu", dwErrorCode);
                KERNEL_ASSERT(0);
            }
            else
            {
                bResult = true;
            }
        }
        else
        {
            SetError();
            ShowLog(3, "WriteFileEx Failed, Error: %lu", GetLastError());
            KERNEL_ASSERT(0);
        }

        return bResult;
    }

    bool CSerialPort::ClosePort()
    {
        if (m_ucom != INVALID_HANDLE_VALUE)
        {
            //关闭与串口对应句柄句柄
            CloseHandle(m_ucom);
            m_ucom = INVALID_HANDLE_VALUE;
        }
        return true;
    }

    void CSerialPort::CloseEvent()
    {
        if (m_hEvent != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_hEvent);
            m_hEvent = INVALID_HANDLE_VALUE;
        }
    }

    void CSerialPort::SetError()
    {
        m_error = true;
        SetEvent(m_hEvent);
    }

    void CSerialPort::Run()
    {
        InitEvent();
        if (InitPort(m_comPort.c_str()))
        {
            AsyncRead();
            while (!m_end)
            {
                DWORD dwWait = WaitForSingleObjectEx(
                    m_hEvent,  // event object to wait for 
                    INFINITE,       // waits indefinitely 
                    TRUE);          // alertable wait enabled 

                switch (dwWait)
                {
                    case 0:
                    {
                        if (!m_error)
                        {
                            AsyncWirte();
                        }
                        else
                        {
                            m_error = false;
                            ClosePort();
                            if (InitPort(m_comPort.c_str()))
                            {
                                AsyncRead();
                            }
                            else
                            {
                                SetError();
                                Sleep(1000);
                            }
                        }
                    }
                    break;

                    case WAIT_IO_COMPLETION:
                    {
                    }
                    break;

                    default:
                    {
                        ShowLog(3, "WaitForSingleObjectEx, Error: %lu", GetLastError());
                    }
                }
            }
        }
        ClosePort();
        CloseEvent();
    }


    VOID WINAPI CompletedWriteRoutine(DWORD dwErr, DWORD dwWriteBytes,
        LPOVERLAPPED lpOverLap)
    {
        CSerialPort* pSerialPort = (CSerialPort*)lpOverLap->Pointer;
        pSerialPort->OnWriteComplete(dwWriteBytes);
    }

    VOID WINAPI CompletedReadRoutine(DWORD dwErr, DWORD dwReadBytes,
        LPOVERLAPPED lpOverLap)
    {
        CSerialPort* pSerialPort = (CSerialPort*)lpOverLap->Pointer;
        pSerialPort->OnReadComplete(dwReadBytes);
    }

}