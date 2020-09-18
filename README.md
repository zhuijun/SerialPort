# SerialPort

#### 介绍
Windows串口异步读写

#### 软件架构
软件架构说明

1、在 CSerialPort::InitPort 里配置串口相关参数
2、传入 funcCompletedReadCallback 作为接收到串口数据的处理函数
3、调用 CSerialPort::Start 启动
4、调用 CSerialPort::Push 发送数据
