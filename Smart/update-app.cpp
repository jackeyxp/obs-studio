
#include "update-app.h"
#include "window-main.hpp"

void doCloseMainWindow()
{
	// 这里需要告诉主窗口不要弹出询问框，直接退出主进程就可以了...
	OBSMainWindow * main = reinterpret_cast<OBSMainWindow*>(App()->GetMainWindow());
	CLoginMini * mini = reinterpret_cast<CLoginMini*>(App()->GetLoginMini());
	if (main != NULL) {
		// 设置主窗口沉默退出...
		main->SetSlientClose(true);
		// 向主窗口发送退出信号...
		QMetaObject::invokeMethod(main, "close");
	} else if (mini != NULL) {
		// 向主窗口发送退出信号...
		QMetaObject::invokeMethod(mini, "close");
	}
}
