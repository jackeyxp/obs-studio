
#include "app.h"
 
// STL must use g++...
// g++ -g udpcenter.c ../common/bmem.c ../common/server.c ../common/thread.cpp app.cpp tcpclient.cpp tcpthread.cpp -o udpcenter -lrt -lpthread -ljson
// valgrind --tool=memcheck --leak-check=full --show-reachable=yes ./udpcenter

CApp theApp;

#define LOG_MAX_SIZE                2048            // 单条日志最大长度(字节)...
#define LOG_MAX_FILE    40 * 1024 * 1024            // 日志文件最大长度(字节)...

char g_absolute_path[256] = {0};
char g_log_file_path[256] = {0};
void doSliceLogFile(struct tm * lpCurTm);
bool doGetCurFullPath(char * lpOutPath, int inSize);

int main(int argc, char **argv)
{
  // 获取进程当前运行完整路径...
  if( !doGetCurFullPath(g_absolute_path, 256) )
    return -1;
  // 构造日志文件完整路径，并打印服务器版本信息...
  sprintf(g_log_file_path, "%s%s", g_absolute_path, "udpcenter.log");
  log_trace("[UDPCenter] version => %s", SERVER_VERSION);
  // 增大文件打开数量...
  if( !theApp.doInitRLimit() )
    return -1;
  // 启动TCP监听线程对象...
  if( !theApp.doStartThread() )
    return -1;
  // 循环进行超时检测...
  theApp.doWaitForExit();
  // 终止，退出进程...
  return 0;
}

// 返回全局的App对象...
CApp * GetApp() { return &theApp; }

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
  sprintf(szNewSliceName, "%s%s_%d-%02d-%02d_%02d-%02d-%02d.log", g_absolute_path, "udpcenter",
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
