#pragma once
#include<Windows.h>
#include<string>
#include<iostream>
#include <queue>
#include <mutex>
#include <thread>
#include <functional>

namespace SerialPort
{

    struct Package
    {
        Package(void* addr, DWORD size);
        ~Package();

        Package(Package&& other);
        Package(const Package&) = delete;

        BYTE* data = nullptr;
        DWORD len = 0;
    };

    //读取数据后的回调函数
    using CompletedReadCallback = std::function<void(const BYTE* data, DWORD len)>;

    class CSerialPort
    {
    public:
        CSerialPort(CompletedReadCallback funcCompletedReadCallback);
        ~CSerialPort();

        void Push(void* addr, DWORD size);

        bool Start(const char* comPort);
        void Stop();

        void OnWriteComplete(DWORD dwWriteBytes);
        void OnReadComplete(DWORD dwReadBytes);

    private:

        void AsyncWirte();
        void AsyncRead();

        /***************************************************************************
        *  功能： 读取串口
        *
        *  @brief 异步数据读取请求
        *  @param addr: 指定数据地址
        *  @param size：指定读取大小
        *  @return 正常读取时，返回true，失败则返回flase
        *  @note
        *
        ****************************************************************************/
        bool ReadPort(void* addr, DWORD size);



        /***************************************************************************
        *  功能： 写入串口
        *
        *  @brief 异步数据写入请求
        *  @param addr: 指定数据地址
        *  @param size：指定写入大小
        *  @return 正常写入时，返回true，失败则返回flase
        *  @note
        *
        ****************************************************************************/
        bool WritePort(void* addr, DWORD size);


        /***************************************************************************
        *  功能： 打开串口
        *
        *  @brief 创建串口对应的句柄
        *  @return 创建成功返回true，失败则返回flase
        *  @note
        *
        ****************************************************************************/
        bool OpenPort(const char* comPort);


        /***************************************************************************
        *  功能： 初始化串口
        *
        *  @brief 内部调用成员函数OpenPort()，一共进行五步操作：
        *	 （1）创建与串口对应的文件句柄；
        *	 （2）获取串口当前参数；
        *    （3）重新设置串口参数；
        *    （4）设置串口接受和发送缓冲区大小（可选）；
        *    （5）清空发送接收缓冲区创建串口对应的句柄
        *  @return 返回false的情况（条件之间为“或”的关系）：
        *	   1、创建句柄失败；
        *	   2、设置串口连接信息失败；
        *	   3、设置收发缓冲器大小失败；
        *	   4：清空收发缓冲器失败
        *  初始化成功返回true
        *  @note
        *
        ****************************************************************************/
        bool InitPort(const char* comPort);

        /***************************************************************************
        *  功能： 关闭串口
        *
        *  @brief 关闭与串口对应的句柄
        *  @return 关闭成功返回true，失败则返回flase
        *  @note
        *
        ****************************************************************************/
        bool ClosePort();

        void InitEvent();

        void CloseEvent();

        void SetError();

        void Run();

    private:
        std::string m_comPort;
        std::thread m_thread;
        bool m_end = false;
        bool m_error = false;

        //与COM对应的句柄
        HANDLE m_ucom;

        OVERLAPPED m_oRead;
        OVERLAPPED m_oWrite;

        HANDLE m_hEvent;

        std::queue<Package> m_writeQueue;
        std::mutex m_wirteMutex;

        BYTE m_readBuffer[1024];
        DWORD m_readLength;
        CompletedReadCallback m_funcCompletedReadCallback;

        int m_ref = 0;
    };

}