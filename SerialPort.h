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

    //��ȡ���ݺ�Ļص�����
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
        *  ���ܣ� ��ȡ����
        *
        *  @brief �첽���ݶ�ȡ����
        *  @param addr: ָ�����ݵ�ַ
        *  @param size��ָ����ȡ��С
        *  @return ������ȡʱ������true��ʧ���򷵻�flase
        *  @note
        *
        ****************************************************************************/
        bool ReadPort(void* addr, DWORD size);



        /***************************************************************************
        *  ���ܣ� д�봮��
        *
        *  @brief �첽����д������
        *  @param addr: ָ�����ݵ�ַ
        *  @param size��ָ��д���С
        *  @return ����д��ʱ������true��ʧ���򷵻�flase
        *  @note
        *
        ****************************************************************************/
        bool WritePort(void* addr, DWORD size);


        /***************************************************************************
        *  ���ܣ� �򿪴���
        *
        *  @brief �������ڶ�Ӧ�ľ��
        *  @return �����ɹ�����true��ʧ���򷵻�flase
        *  @note
        *
        ****************************************************************************/
        bool OpenPort(const char* comPort);


        /***************************************************************************
        *  ���ܣ� ��ʼ������
        *
        *  @brief �ڲ����ó�Ա����OpenPort()��һ�������岽������
        *	 ��1�������봮�ڶ�Ӧ���ļ������
        *	 ��2����ȡ���ڵ�ǰ������
        *    ��3���������ô��ڲ�����
        *    ��4�����ô��ڽ��ܺͷ��ͻ�������С����ѡ����
        *    ��5����շ��ͽ��ջ������������ڶ�Ӧ�ľ��
        *  @return ����false�����������֮��Ϊ���򡱵Ĺ�ϵ����
        *	   1���������ʧ�ܣ�
        *	   2�����ô���������Ϣʧ�ܣ�
        *	   3�������շ���������Сʧ�ܣ�
        *	   4������շ�������ʧ��
        *  ��ʼ���ɹ�����true
        *  @note
        *
        ****************************************************************************/
        bool InitPort(const char* comPort);

        /***************************************************************************
        *  ���ܣ� �رմ���
        *
        *  @brief �ر��봮�ڶ�Ӧ�ľ��
        *  @return �رճɹ�����true��ʧ���򷵻�flase
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

        //��COM��Ӧ�ľ��
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