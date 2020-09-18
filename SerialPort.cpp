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
        std::string com_port = "\\\\.\\";//������Ը���ʵ��COM��������,�������õ�COM8
        com_port.append(comPort);
        //CreateFileA��Ӧ�������ݲ���ASCII�����ʽ��CreateFileW��Ӧ�������ݲ���UNICODE�����ʽ
        m_ucom = CreateFileA(com_port.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
        if (m_ucom == INVALID_HANDLE_VALUE)
        {
            return false;

        }
        return true;
    }

    bool CSerialPort::InitPort(const char* comPort)
    {
        //�򿪴���
        if (!OpenPort(comPort))
        {
            ShowLog(3, "Open Serial Port Failed  error: %lu", GetLastError());
            return false;
        }

        DCB dcb;//�ṹ��������ͣ�����������Ϣ�涨ʹ���������ͣ���ԱΪ������������ѡ�����������,����λ���ȵ�

        memset(&dcb, 0X0, sizeof(dcb));
        GetCommState(m_ucom, &dcb);

        dcb.BaudRate = 38400;  //����������Ϊ38400
        dcb.ByteSize = 8;       //8λ����λ            * 4-8��ѡ
        dcb.StopBits = 0;       //ֹͣλ             * 0:1λֹͣλ 1��1.5λֹͣλ 2��2λֹͣλ
        dcb.fParity = 0;        //��������żУ��       * 0����У�� 1��У��
        dcb.fNull = 0;          //������մ�           * 0�������� 1������
        dcb.Parity = 0;         //������У��           * 0����У�� 1����У�� 2��żУ�� 3��mark 4:space

        //���ô���������Ϣ,��д�����С����Ĭ������
        if (!SetCommState(m_ucom, &dcb))
        {
            ShowLog(3, "Set Serial port error: %lu", GetLastError());
            return false;
        }

        //���ö�д��������С����ѡ�����ñ�ʾ����Ĭ������
        if (!SetupComm(m_ucom, 1048576, 1048576))
        {
            ShowLog(3, "Set the Size of Serial Receive Buffer and Send Buffer Failed, Error: %lu", GetLastError());
            return false;
        }

        //��ʱ����,��λ������
        //�ܳ�ʱ��ʱ��ϵ��������д���ַ�����ʱ�䳣��
        COMMTIMEOUTS TimeOuts;
        TimeOuts.ReadIntervalTimeout = 10; //�������ʱ
        TimeOuts.ReadTotalTimeoutMultiplier = 50; //��ʱ��ϵ��
        TimeOuts.ReadTotalTimeoutConstant = 5000; //��ʱ�䳣��
        TimeOuts.WriteTotalTimeoutMultiplier = 500; // дʱ��ϵ��
        TimeOuts.WriteTotalTimeoutConstant = 2000; //дʱ�䳣��
        SetCommTimeouts(m_ucom, &TimeOuts);

        // ��մ���
        COMSTAT comstat;
        DWORD dwErrFlags = 0;
        ClearCommError(m_ucom, &dwErrFlags, &comstat);

        //�����շ��ͽ��ջ�����
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
            //�ر��봮�ڶ�Ӧ������
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