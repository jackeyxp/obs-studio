
#include "app.h"
#include "getopt.h"
#include <signal.h>
#include "tcpthread.h"
#include "udpthread.h"
#include "../common/bmem.h"

CApp::CApp()
  : m_lpTCPThread(NULL)
  , m_lpUDPThread(NULL)
  , m_signal_quit(false)
  , m_bIsDebugMode(false)
  , m_sem_t(NULL)
{
  m_strTCPCenterAddr = DEF_CENTER_ADDR;
  m_nTCPCenterPort = DEF_CENTER_PORT;
  m_nUDPListenPort = DEF_UDP_PORT;
  m_nTCPListenPort = DEF_TCP_PORT;
  // 初始化辅助线程信号量...
  os_sem_init(&m_sem_t, 0);
}

CApp::~CApp()
{
  // 释放所有的资源对象...
  this->clearAllSource();
  // 打印最终通过bmem分配的内存是否完全释放...
  log_trace("Number of memory leaks: %ld", bnum_allocs());
}

// 调用位置，详见 udpserver.c::main() 函数，只调用一次...
bool CApp::doProcessCmdLine(int argc, char * argv[])
{
  int	 ch = 0;
  bool bExitFlag = false;
  while ((ch = getopt(argc, argv, "?hvdrsc")) != EOF)
  {
    switch (ch)
    {
    case 'd': m_bIsDebugMode = true; continue;
    case 'r': m_bIsDebugMode = false; continue;
    case 'c': m_strTCPCenterAddr = argv[optind++]; continue;
    case 's': this->doStopSignal(); bExitFlag = true; break;
    case '?':
    case 'h':
    case 'v':
      log_trace("-c x.x.x.x => center server address or realm name.");
      log_trace("-d: Run as Debug Mode => mount on Debug student and Debug teacher.");
      log_trace("-r: Run as Release Mode => mount on Release student and Release teacher.");
      log_trace("-s: Send SIG signal to shutdown udpserver.");
      bExitFlag = true;
      break;
    }
  }
  return bExitFlag;
}

void CApp::doStopSignal()
{
  // 打印正在停止进程提示信息...
  log_trace("stoping udpserver...");
  // 从pid文件中读取pid的值...
  int pid = this->read_pid_file();
  // pid无效，直接返回...
  if( pid < 0 )
    return;
  // 发送程序正常结束信号...
  if( kill(pid, SIGTERM) != -1 ) {
    log_trace("udpserver stopped by SIGTERM, pid=%d", pid);
    return;
  }
  // 发送程序强制结束信号...
  if( kill(pid, SIGKILL) != -1 ) {
    log_trace("udpserver stopped by SIGKILL, pid=%d", pid);
    return;
  }
  // 无法结束指定的进程...
  log_trace("udpserver can't be stop, pid=%d", pid);
}

bool CApp::check_pid_file()
{
  // 如果没有读取到pid，返回true...
  int pid = this->read_pid_file();
  if( pid <= 0 ) return true;
  // 读取到了pid文件，打印信息，返回false...
  log_trace("udpserver is running, pid=%d", pid);
  return false;
}

int CApp::read_pid_file()
{
  char pid_path[256] = {0};
  sprintf(pid_path, "%s%s", get_abs_path(), DEFAULT_PID_FILE);
  int fd = open(pid_path, O_RDONLY, S_IRWXU | S_IRGRP | S_IROTH);
  if( fd < 0 ) {
    log_trace("open pid file %s error, ret=%#x", pid_path, errno);
    return -1;
  }
  int  pid   = -1;
  int  nRead = -1;
  char buf[256] = {0};
  do {
    // 移动文件到最前面...
    if( lseek(fd, 0, SEEK_SET) < 0 ) {
      log_trace("lseek pid file %s error, ret=%d", pid_path, errno);
      break;
    }
    // 读取pid文件内容...
    if( (nRead = read(fd, buf, 256)) < 0 ) {
      log_trace("read from file %s failed. ret=%d", pid_path, errno);
      break;
    }
    // 转换字符串为数字...
    if( (pid = atoi(buf)) <= 0 ) {
      log_trace("read from file %s failed. error=%s", pid_path, buf);
      break;
    }
  } while( false );
  // 关闭文件句柄对象...
  close(fd); fd = -1;
  // 返回读取到的pid值...
  return pid;
}

bool CApp::destory_pid_file()
{
  char pid_path[256] = {0};
  sprintf(pid_path, "%s%s", get_abs_path(), DEFAULT_PID_FILE);
  if( unlink(pid_path) < 0 ) {
    log_trace("unlink pid file %s error, ret=%d", pid_path, errno);
  }
  //log_trace("unlink pid file %s success.", pid_path);
  return true;
}

bool CApp::acquire_pid_file()
{
  char pid_path[256] = {0};
  sprintf(pid_path, "%s%s", get_abs_path(), DEFAULT_PID_FILE);

  // -rw-r--r-- 
  // 644
  int mode = S_IRUSR | S_IWUSR |  S_IRGRP | S_IROTH;
  
  int fd = -1;
  // open pid file
  if ((fd = ::open(pid_path, O_WRONLY | O_CREAT, mode)) < 0) {
    log_trace("open pid file %s error, ret=%#x", pid_path, errno);
    return false;
  }
  
  // require write lock
  struct flock lock;

  lock.l_type = F_WRLCK; // F_RDLCK, F_WRLCK, F_UNLCK
  lock.l_start = 0; // type offset, relative to l_whence
  lock.l_whence = SEEK_SET;  // SEEK_SET, SEEK_CUR, SEEK_END
  lock.l_len = 0;
  
  if (fcntl(fd, F_SETLK, &lock) < 0) {
    if(errno == EACCES || errno == EAGAIN) {
      log_trace("udpserver is already running! ret=%#x", errno);
      ::close(fd); fd = -1;
      return false;
    }
    log_trace("require lock for file %s error! ret=%#x", pid_path, errno);
    ::close(fd); fd = -1;
    return false;
  }

  // truncate file
  if (ftruncate(fd, 0) < 0) {
    log_trace("truncate pid file %s error! ret=%#x", pid_path, errno);
    ::close(fd); fd = -1;
    return false;
  }

  int pid = (int)getpid();
  
  // write the pid
  char buf[256] = {0};
  snprintf(buf, sizeof(buf), "%d", pid);
  if (write(fd, buf, strlen(buf)) != (int)strlen(buf)) {
    log_trace("write our pid error! pid=%d file=%s ret=%#x", pid, pid_path, errno);
    ::close(fd); fd = -1;
    return false;
  }

  // auto close when fork child process.
  int val = 0;
  if ((val = fcntl(fd, F_GETFD, 0)) < 0) {
    log_trace("fnctl F_GETFD error! file=%s ret=%#x", pid_path, errno);
    ::close(fd); fd = -1;
    return false;
  }
  val |= FD_CLOEXEC;
  if (fcntl(fd, F_SETFD, val) < 0) {
    log_trace("fcntl F_SETFD error! file=%s ret=%#x", pid_path, errno);
    ::close(fd); fd = -1;
    return false;
  }
  
  log_trace("write pid=%d to %s success!", pid, pid_path);
  ::close(fd); fd = -1;
  
  return true;
}

void CApp::clearAllSource()
{
  // 删除TCP线程对象...
  if (m_lpTCPThread != NULL) {
    delete m_lpTCPThread;
    m_lpTCPThread = NULL;
  }
  // 删除UDP线程对象...
  if (m_lpUDPThread != NULL) {
    delete m_lpUDPThread;
    m_lpUDPThread = NULL;
  }
  // 释放辅助线程信号量...
  if (m_sem_t != NULL) {
    os_sem_destroy(m_sem_t);
    m_sem_t = NULL;
  }
}

void CApp::onSignalQuit()
{
  // 设置退出标志...
  m_signal_quit = true;
  // 触发信号量，快速退出...
  os_sem_post(m_sem_t);
}

bool CApp::doStartThread()
{
  // 创建TCP监听对象，并启动线程...
  assert(m_lpTCPThread == NULL);
  m_lpTCPThread = new CTCPThread();
  if (!m_lpTCPThread->InitThread()) {
    log_trace("Init TCPThread failed!");
    return false;
  }
  // 创建UDP数据线程，并启动线程...
  assert(m_lpUDPThread == NULL);
  m_lpUDPThread = new CUDPThread();
  if (!m_lpUDPThread->InitThread()) {
    log_trace("Init UDPThread failed!");
    return false;
  }
  return true;
}

bool CApp::doInitRLimit()
{
	// set max open file number for one process...
	struct rlimit rt = {0};
	rt.rlim_max = rt.rlim_cur = MAX_OPEN_FILE;
	if( setrlimit(RLIMIT_NOFILE, &rt) == -1 ) {
    log_trace("setrlimit error(%s)", strerror(errno));
		return false;
	}
  return true;
}

void CApp::doWaitForExit()
{
  // 设定默认的信号超时时间 => APP_SLEEP_MS 毫秒...
  unsigned long next_wait_ms = APP_SLEEP_MS;
  // 判断是否有信号退出标志...
  while ( !this->IsSignalQuit() ) {
    // 注意：这里用信号量代替sleep的目的是为了避免等待时的命令延时...
    // 注意：无论信号量是超时还是被触发，都要执行下面的操作...
    os_sem_timedwait(m_sem_t, next_wait_ms);
  }
  // 预先释放所有分配的线程和资源...
  this->clearAllSource();
  // 删除相关联的pid文件...
  this->destory_pid_file();
  // 打印已经成功退出信息...
  log_trace("cleanup for gracefully terminate.");
}

// 注意：阿里云专有网络无法获取外网地址，中心服务器可以同链接获取外网地址...
// 因此，这个接口作废了，不会被调用，而是让中心服务器通过链接地址自动获取...
/*bool CApp::doInitWanAddr()
{
  struct ifaddrs *ifaddr, *ifa;
  char host_ip[NI_MAXHOST] = {0};
  int family, result, is_ok = 0;
  if( getifaddrs(&ifaddr) == -1) {
    return false;
  }
  for( ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next ) { 
    if( ifa->ifa_addr == NULL )
      continue;
    family = ifa->ifa_addr->sa_family;
    if( family != AF_INET )
      continue;
    result = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host_ip, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
    if( result != 0 )
      continue;
    // 先排除本机的环路地址...
    if( strcasecmp(host_ip, "127.0.0.1") == 0 )
      continue;
    // 将获取的IP地址进行转换判断...
    uint32_t nHostAddr = ntohl(inet_addr(host_ip));
    // 检查是否是以下三类内网地址...
    // A类：10.0.0.0 ~ 10.255.255.255
    // B类：172.16.0.0 ~ 172.31.255.255
    // C类：192.168.0.0 ~ 192.168.255.255
    if((nHostAddr >= 0x0A000000 && nHostAddr <= 0x0AFFFFFF) ||
      (nHostAddr >= 0xAC100000 && nHostAddr <= 0xAC1FFFFF) ||
      (nHostAddr >= 0xC0A80000 && nHostAddr <= 0xC0A8FFFF))
      continue;
    // 不是三类内网地址，说明找到了本机的外网地址...
    is_ok = 1;
    break;
  }
  // 释放资源，没有找到，直接返回...
  freeifaddrs(ifaddr);
  if( !is_ok ) {
    return false;
  }
  // 如果汇报地址host_ip为空，打印错误，返回...
  if( strlen(host_ip) <= 0 ) {
    log_trace("Error: host_ip is empty ==");
    return false;
  }
  // 保存外网地址...
  m_strWanAddr = host_ip;
  return true;
}*/

/*void CApp::Entry()
{
  // 设定默认的信号超时时间 => APP_SLEEP_MS 毫秒...
  unsigned long next_wait_ms = APP_SLEEP_MS;
  uint64_t next_check_ns = os_gettime_ns();
  uint64_t next_detect_ns = next_check_ns;
  while( !this->IsStopRequested() ) {
    // 注意：这里用信号量代替sleep的目的是为了避免补包发生时的命令延时...
    // 无论信号量是超时还是被触发，都要执行下面的操作...
    //log_trace("[App] start, sem-wait: %d", next_wait_ms);
    os_sem_timedwait(m_sem_t, next_wait_ms);
    //log_trace("[App] end, sem-wait: %d", next_wait_ms);
    // 进行补包对象的补包检测处理 => 返回休息毫秒数...
    int nRetSupply = this->doSendSupply();
    // 进行学生观看端的丢包处理过程...
    int nRetLose = this->doSendLose();
    // 取两者当中最小的时间做为等待时间...
    next_wait_ms = min(nRetSupply, nRetLose);
    // 等待时间区间 => [0, APP_SLEEP_MS]毫秒...
    assert(next_wait_ms >= 0 && next_wait_ms <= APP_SLEEP_MS);
    // 当前时间与上次检测时间之差 => 转换成秒...
    uint64_t cur_time_ns = os_gettime_ns();
    // 每隔 1 秒服务器向所有终端发起探测命令包...
    if( (cur_time_ns - next_detect_ns)/1000000 >= 1000 ) {
      this->doServerSendDetect();
      next_detect_ns = cur_time_ns;
    }
    // 每隔 10 秒检测一次对象超时 => 检测时间未到，等待半秒再检测...
    int nDeltaSecond = (int)((cur_time_ns- next_check_ns)/1000000000);
    if( nDeltaSecond >= CHECK_TIME_OUT ) {
      this->doCheckTimeout();
      next_check_ns = cur_time_ns;
    }
  }
}

int CApp::doSendLose()
{
  // 线程互斥锁定 => 是否补过包...
  int n_sleep_ms  = APP_SLEEP_MS;
  pthread_mutex_lock(&m_mutex);
  GM_ListStudent::iterator itorItem = m_ListStudent.begin();
  while( itorItem != m_ListStudent.end() ) {
    // 执行发送丢包数据内容，返回是否还要执行丢包...
    bool bSendResult = (*itorItem)->doServerSendLose();
    // true => 还有丢包要发送，不能休息...
    if( bSendResult ) {
      ++itorItem;
      n_sleep_ms = min(n_sleep_ms, 0);
    } else {
      // false => 没有丢包要发了，从队列当中删除...
      m_ListStudent.erase(itorItem++);
      n_sleep_ms = min(n_sleep_ms, APP_SLEEP_MS);
    }
  }  
  // 如果队列已经为空 => 休息100毫秒...
  if( m_ListStudent.size() <= 0 ) {
    n_sleep_ms = APP_SLEEP_MS;
  }
  // 线程互斥解锁...
  pthread_mutex_unlock(&m_mutex);
  // 返回最终计算的休息毫秒数...
  return n_sleep_ms;
}

int CApp::doSendSupply()
{
  // 线程互斥锁定 => 是否补过包...
  int n_sleep_ms  = APP_SLEEP_MS;
  // 补包发送结果 => -1(删除)0(没发)1(已发)...
  pthread_mutex_lock(&m_mutex);  
  GM_ListTeacher::iterator itorItem = m_ListTeacher.begin();
  while( itorItem != m_ListTeacher.end() ) {
    // 执行补包命令，返回执行结果...
    int nSendResult = (*itorItem)->doServerSendSupply();
    // -1 => 没有补包了，从列表中删除...
    if( nSendResult < 0 ) {
      m_ListTeacher.erase(itorItem++);
      n_sleep_ms = min(n_sleep_ms, APP_SLEEP_MS);
      continue;
    }
    // 继续检测下一个有补包的老师推流端...
    ++itorItem;
    // 0 => 有补包，但是不到补包时间 => 休息15毫秒...
    if( nSendResult == 0 ) {
      n_sleep_ms = min(n_sleep_ms, MAX_SLEEP_MS);
      continue;
    }
    // 1 => 有补包，已经发送补包命令 => 不要休息...
    if( nSendResult > 0 ) {
      n_sleep_ms = min(n_sleep_ms, 0);
    }
  }
  // 如果队列已经为空 => 休息100毫秒...
  if( m_ListTeacher.size() <= 0 ) {
    n_sleep_ms = APP_SLEEP_MS;
  }
  // 线程互斥解锁...
  pthread_mutex_unlock(&m_mutex);
  // 返回最终计算的休息毫秒数...
  return n_sleep_ms;
}
//
// 遍历所有对象，发起探测命令...
void CApp::doServerSendDetect()
{
  // 线程互斥锁定...
  pthread_mutex_lock(&m_mutex);  
  CNetwork * lpNetwork = NULL;
  GM_MapNetwork::iterator itorItem = m_MapNetwork.begin();
  while( itorItem != m_MapNetwork.end() ) {
    lpNetwork = itorItem->second;
    lpNetwork->doServerSendDetect();
    ++itorItem;
  }
  // 线程互斥解锁...
  pthread_mutex_unlock(&m_mutex);
}
//
// 遍历对象，进行超时检测...
void CApp::doCheckTimeout()
{
  // 线程互斥锁定...
  pthread_mutex_lock(&m_mutex);  
  // 遍历对象，进行超时检测...
  CNetwork * lpNetwork = NULL;
  GM_MapNetwork::iterator itorItem = m_MapNetwork.begin();
  while( itorItem != m_MapNetwork.end() ) {
    lpNetwork = itorItem->second;
    if( lpNetwork->IsTimeout() ) {
      // 打印删除消息，删除对象 => 在析构函数中对房间信息进行清理工作...
      delete lpNetwork; lpNetwork = NULL;
      m_MapNetwork.erase(itorItem++);
    } else {
      ++itorItem;
    }
  }
  // 线程互斥解锁...
  pthread_mutex_unlock(&m_mutex);
}*/

/*void CApp::doWaitSocket()
{
  while (m_listen_fd > 0) {
    // 设置休息标志 => 只要有发包或收包就不能休息...
    m_bNeedSleep = true;
    // 每隔 1 秒服务器向所有终端发起探测命令包...
    this->doSendDetectCmd();
    // 每隔 10 秒检测一次对象超时...
    this->doCheckTimeout();
    // 接收一个到达的UDP数据包...
    this->doRecvPacket();
    // 先发送针对讲师的补包命令...
    this->doSendSupplyCmd();
    // 再发送针对学生的丢包命令...
    this->doSendLoseCmd();
    // 等待发送或接收下一个数据包...
    this->doSleepTo();
  }
}*/
