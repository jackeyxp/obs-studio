
#include "app.h"
#include "udpthread.h"

CUDPThread::CUDPThread()
  : m_udp_listen_fd(0)
{
}

CUDPThread::~CUDPThread()
{
  // 必须先设置标志，确保套接字关闭后，线程必然退出...
  this->SendStopRequest();
  // 先关闭套接字，迫使阻塞线程退出...
  if( m_udp_listen_fd > 0 ) {
    close(m_udp_listen_fd);
    m_udp_listen_fd = 0;
  }
  // 关闭套接字之后，等待线程退出...
  this->StopAndWaitForThread();
}

bool CUDPThread::InitThread()
{
  // 获取UDP监听端口配置，创建UDP监听套接字...
  int nHostPort = GetApp()->GetUdpListenPort();
  if( this->doCreateListenSocket(nHostPort) < 0 )
    return false;
  
  // 打印中心服务器正在监听的UDP端口信息...
  log_trace("[UDPServer] udp listen port => %d", nHostPort);

  // 启动udp服务器监听线程...
  this->Start();
  return true;
}

int CUDPThread::doCreateListenSocket(int nHostPort)
{
  // 创建UDP监听套接字...
  int listen_fd = socket(AF_INET, SOCK_DGRAM, 0); 
  if( listen_fd < 0 ) {
    log_trace("can't create udp socket");
    return -1;
  }
  // 2018.12.17 - by jackey => 用同步模式...
  // 设置异步UDP套接字 => 失败，关闭套接字...
  //if( fcntl(listen_fd, F_SETFL, fcntl(listen_fd, F_GETFD, 0)|O_NONBLOCK) == -1 ) {
  //  log_trace("O_NONBLOCK error: %s", strerror(errno));
  //  close(listen_fd);
  //  return -1;
  //}
 	int opt = 1;
  // 设置地址重用 => 失败，关闭套接字...
  if( setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0 ) {
    log_trace("SO_REUSEADDR error: %s", strerror(errno));
    close(listen_fd);
    return -1;
  }
  // 设置端口重用 => 失败，关闭套接字...
  if( setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) != 0 ) {
    log_trace("SO_REUSEPORT error: %s", strerror(errno));
    close(listen_fd);
    return -1;
  }
  // 设定发送和接收缓冲最大值...
  int nRecvMaxLen = 256 * 1024;
  int nSendMaxLen = 256 * 1024;
  // 设置接收缓冲区...
  if( setsockopt(listen_fd, SOL_SOCKET, SO_RCVBUF, &nRecvMaxLen, sizeof(nRecvMaxLen)) != 0 ) {
    log_trace("SO_RCVBUF error: %s", strerror(errno));
    close(listen_fd);
    return -1;
  }
  // 设置发送缓冲区...
  if( setsockopt(listen_fd, SOL_SOCKET, SO_SNDBUF, &nSendMaxLen, sizeof(nSendMaxLen)) != 0 ) {
    log_trace("SO_SNDBUF error: %s", strerror(errno));
    close(listen_fd);
    return -1;
  }
  // 准备绑定地址结构体...
  struct sockaddr_in udpAddr = {0};
  bzero(&udpAddr, sizeof(udpAddr));
  udpAddr.sin_family = AF_INET; 
  udpAddr.sin_port = htons(nHostPort);
  udpAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  // 绑定监听端口...
  if( bind(listen_fd, (struct sockaddr *)&udpAddr, sizeof(struct sockaddr)) == -1 ) {
    log_trace("bind udp port: %d, error: %s", nHostPort, strerror(errno));
    close(listen_fd);
    return -1;
  }
  // 打印服务器正在监听的UDP端口信息...
  log_trace("[UDPServer] udp listen port => %d", nHostPort);
  // 返回已经绑定完毕的UDP套接字...
  m_udp_listen_fd = listen_fd;
  return m_udp_listen_fd;
}

void CUDPThread::Entry()
{
  // 打印UDP监听线程正常启动提示信息...
  log_trace("udp-thread startup, udp-port %d", GetApp()->GetUdpListenPort());
  // UDP网络层接收数据需要的变量...
  struct sockaddr_in recvAddr = {0};
  char recvBuff[MAX_BUFF_LEN] = {0};
  int nAddrLen = 0, nRecvCount = 0;
  // 判断线程是否退出，最终是通过套接字的有效性来判断的...
  while( !this->IsStopRequested() ) {
    // 从网络层阻塞接收UDP数据报文...
    bzero(recvBuff, MAX_BUFF_LEN);
    nAddrLen = sizeof(recvAddr);
    nRecvCount = recvfrom(m_udp_listen_fd, recvBuff, MAX_BUFF_LEN, 0, (sockaddr*)&recvAddr, (socklen_t*)&nAddrLen);
    /////////////////////////////////////////////////////////////////////////////////////
    // 如果返回长度与输入长度一致 => 说明发送端数据越界 => 超过了系统实际处理长度...
    // 注意：出现这种情况，一定要排查发送端的问题 => 通常是序号越界造成的...
    /////////////////////////////////////////////////////////////////////////////////////
    int nMaxSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
    if( nRecvCount > nMaxSize ) {
      log_debug("Error Packet Excessed");
      continue;
    }
    // 发生错误，打印并退出...
    if( nRecvCount <= 0 ) {
      log_trace("recvfrom error(code:%d, %s)", errno, strerror(errno));
      // is EINTR or EAGAIN, continue...
      if( errno == EINTR || errno == EAGAIN ) 
        continue;
      // not EINTR or EAGAIN, break...
      break;
    }
    // 获取发送者映射的地址和端口号 => 后期需要注意端口号变化的问题...
    uint32_t nHostSinAddr = ntohl(recvAddr.sin_addr.s_addr);
    uint16_t nHostSinPort = ntohs(recvAddr.sin_port);
    // 将网络接收到的数据包投递给主线程处理...
    //GetApp()->onRecvEvent(nHostSinAddr, nHostSinPort, recvBuff, nRecvCount);
  }
  // 打印UDP线程退出信息...
  log_trace("udp-thread exit.");
  ////////////////////////////////////////////////////////////////////
  // 注意：房间对象和终端对象的删除，放在了析构函数当中...
  ////////////////////////////////////////////////////////////////////
}
