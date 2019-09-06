
#include "app.h"
#include <signal.h>
//#include <execinfo.h>

// STL must use g++...
// g++ -g udpserver.c ../common/bmem.c ../common/server.c ../common/thread.cpp app.cpp tcpcamera.cpp tcproom.cpp tcpcenter.cpp tcpclient.cpp tcpthread.cpp udpthread.cpp room.cpp network.cpp student.cpp teacher.cpp -o udpserver -lrt -lpthread -ljson
// g++ -g udpserver.c ../common/bmem.c ../common/server.c ../common/thread.cpp app.cpp room.cpp tcpcenter.cpp tcpclient.cpp tcpthread.cpp udpthread.cpp -o udpserver -lrt -lpthread -ljson
// valgrind --tool=helgrind --tool=memcheck --leak-check=full --show-reachable=yes ./udpserver

CApp theApp;

#define LOG_MAX_SIZE                2048            // 单条日志最大长度(字节)...
#define LOG_MAX_FILE    40 * 1024 * 1024            // 日志文件最大长度(字节)...

char g_absolute_path[256] = {0};
char g_log_file_path[256] = {0};

bool doGetCurFullPath(char * lpOutPath, int inSize);
void doSliceLogFile(struct tm * lpCurTm);
void do_sig_catcher(int signo);
void do_err_crasher(int signo);
bool doRegisterSignal();

int main(int argc, char **argv)
{
  // 获取进程当前运行完整路径...
  if( !doGetCurFullPath(g_absolute_path, 256) )
    return -1;
  // 构造日志文件完整路径，并打印服务器版本信息......
  sprintf(g_log_file_path, "%s%s", g_absolute_path, DEFAULT_LOG_FILE);
  log_trace("[UDPServer] version => %s", SERVER_VERSION);
  // 读取命令行各字段内容信息并执行...
  if( theApp.doProcessCmdLine(argc, argv) )
    return -1;
  // 只允许一个进程运行，多个进程会造成混乱...
  if( !theApp.check_pid_file() )
    return -1;
  // 注册信号操作函数...
  if( !doRegisterSignal() )
    return -1;
  // 增大文件打开数量...
  if( !theApp.doInitRLimit() )
    return -1;
  // 创建进程的pid文件...
  if( !theApp.acquire_pid_file() )
    return -1;

  // 注意：阿里云专有网络无法获取外网地址，中心服务器可以通过链接获取外网地址...
  // 因此，这个接口作废了，不会被调用，而是让中心服务器通过链接地址自动获取...
  //if( !theApp.doInitWanAddr() )
  //  return -1;
  
  // 分别启动TCP|UDP线程对象...
  if( !theApp.doStartThread() )
    return -1;
  // 注意：主线程退出时会删除pid文件...
  // 循环进行退出标志检测...
  theApp.doWaitForExit();
  // 阻塞终止，退出进程...
  return 0;
}

// 返回全局的App对象...
CApp * GetApp() { return &theApp; }

// 注册全局的信号处理函数...
bool doRegisterSignal()
{
  struct sigaction sa = {0};
  
  /* Install do_sig_catcher() as a signal handler */
  sa.sa_handler = do_sig_catcher;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGTERM, &sa, NULL);
 
  sa.sa_handler = do_sig_catcher;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, NULL);
  
  /* Install do_err_crasher() as a signal handler */
  sa.sa_handler = do_err_crasher;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGILL, &sa, NULL);   // 非法指令

  sa.sa_handler = do_err_crasher;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGBUS, &sa, NULL);   // 总线错误

  sa.sa_handler = do_err_crasher;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGFPE, &sa, NULL);   // 浮点异常

  sa.sa_handler = do_err_crasher;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGABRT, &sa, NULL);  // 来自abort函数的终止信号

  sa.sa_handler = do_err_crasher;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGSEGV, &sa, NULL);  // 无效的存储器引用(段错误)
  
  return true;
}

void do_sig_catcher(int signo)
{
  log_trace("udpserver catch terminate signal=%d", signo);
  if( signo == SIGTERM || signo == SIGINT ) {
    theApp.onSignalQuit();
  }
}

void do_err_crasher(int signo)
{
  // 还原默认的信号处理...
  signal(signo, SIG_DFL);
  // 打印发生崩溃的信号编号...
  log_trace("udpserver catch err-crash signal=%d", signo);
  //////////////////////////////////////////////////////////////////////////
  // 注意：枚举当前调用堆栈，堆栈信息太少，还要增加编译开关...
  // 注意：需要增加编译开关才会有更详细的函数名称 -rdynamic
  // 注意：不记录崩溃堆栈的原因是由于coredump产生的信息更丰富...
  // 注意：崩溃捕获更重要的作用是做善后处理，比如：删除pid文件...
  //////////////////////////////////////////////////////////////////////////
  /*void * DumpArray[256] = {0};
  int nSize = backtrace(DumpArray, 256);
  char ** lppSymbols = backtrace_symbols(DumpArray, nSize);
  for (int i = 0; i < nSize; i++) {
    log_trace("callstack => %d, %s", i, lppSymbols[i]);
  }*/
  // 最后删除pid文件...
  theApp.destory_pid_file();
}

// 获取当前进程全路径...
bool doGetCurFullPath(char * lpOutPath, int inSize)
{
  // 如果路径已经有效，直接返回...
  if( strlen(lpOutPath) > 0 )
    return true;
  // find the current path...
  int cnt = readlink("/proc/self/exe", lpOutPath, inSize);
  if( cnt < 0 || cnt >= inSize ) {
    fprintf(stderr, "readlink error: %s", strerror(errno));
    return false;
  }
  for( int i = cnt; i >= 0; --i ) {
    if( lpOutPath[i] == '/' ) {
      lpOutPath[i+1] = '\0';
      break;
    }
  }
  // append the slash flag...
  if( lpOutPath[strlen(lpOutPath) - 1] != '/' ) {  
    lpOutPath[strlen(lpOutPath)] = '/';  
  }
  return true;
}

// 对日志进行分片操作...
void doSliceLogFile(struct tm * lpCurTm)
{
  // 如果日志路径、日志文件路径、输入时间戳无效，直接返回...
  if( lpCurTm == NULL || strlen(g_absolute_path) <= 0 || strlen(g_log_file_path) <= 0 )
    return;
  // 获取日志文件的总长度，判断是否需要新建日志...
  struct stat dtLog = {0};
  if( stat(g_log_file_path, &dtLog) < 0 )
    return;
  // 日志文件不超过40M字节，直接返回...
  if( dtLog.st_size <= LOG_MAX_FILE )
    return;
  // 构造分片文件全路径名称...
  char szNewSliceName[256] = {0};
  sprintf(szNewSliceName, "%s%s_%d-%02d-%02d_%02d-%02d-%02d.log", g_absolute_path, "udpserver",
          1900 + lpCurTm->tm_year, 1 + lpCurTm->tm_mon, lpCurTm->tm_mday,
          lpCurTm->tm_hour, lpCurTm->tm_min, lpCurTm->tm_sec);
  // 对日志文件进行重新命名操作...
  rename(g_log_file_path, szNewSliceName);
}

// 全新的日志处理函数...
bool do_trace(const char * inFile, int inLine, bool bIsDebug, const char *msg, ...)
{
  // 获取日志文件全路径是否有效...
  if( strlen(g_log_file_path) <= 0 ) {
    fprintf(stderr, "log_file_path failed\n");
    return false;
  }
  // 准备日志头需要的时间信息...
  timeval tv;
  if(gettimeofday(&tv, NULL) == -1) {
    return false;
  }
  struct tm* tm;
  if((tm = localtime(&tv.tv_sec)) == NULL) {
    return false;
  }
  // 对日志进行分片操作...
  doSliceLogFile(tm);
  // 准备完整的日志头信息...
  int  log_size = -1;
  char log_data[LOG_MAX_SIZE] = {0};
  log_size = snprintf(log_data, LOG_MAX_SIZE, 
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%d][%s:%d][%s] ", 1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec/1000), getpid(), inFile, inLine,
                bIsDebug ? "debug" : "trace");
  // 确认日志头长度...
  if(log_size == -1) {
    return false;
  }
  // 打开日志文件...
  int file_log_fd = -1;
  if( !bIsDebug ) {
    file_log_fd = open(g_log_file_path, O_RDWR|O_CREAT|O_APPEND, 0666);
    if( file_log_fd < 0 ) {
      return false;
    }
  }
  // 对数据进行格式化...
  va_list vl_list;
  va_start(vl_list, msg);
  log_size += vsnprintf(log_data + log_size, LOG_MAX_SIZE - log_size, msg, vl_list);
  va_end(vl_list);
  // 加入结尾符号...
  log_data[log_size++] = '\n';
  // 将格式化之后的数据写入文件...
  if( !bIsDebug ) {
    write(file_log_fd, log_data, log_size);
    close(file_log_fd);
  }
  // 将数据打印到控制台...
  fprintf(stderr, log_data);
  return true;
}

const char * get_abs_path()
{
  return g_absolute_path;
}

