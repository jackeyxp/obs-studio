
#include "update-app.h"
#include "window-main.hpp"

void doCloseMainWindow()
{
	// ������Ҫ���������ڲ�Ҫ����ѯ�ʿ�ֱ���˳������̾Ϳ�����...
	OBSMainWindow * main = reinterpret_cast<OBSMainWindow*>(App()->GetMainWindow());
	CLoginMini * mini = reinterpret_cast<CLoginMini*>(App()->GetLoginMini());
	if (main != NULL) {
		// ���������ڳ�Ĭ�˳�...
		main->SetSlientClose(true);
		// �������ڷ����˳��ź�...
		QMetaObject::invokeMethod(main, "close");
	} else if (mini != NULL) {
		// �������ڷ����˳��ź�...
		QMetaObject::invokeMethod(mini, "close");
	}
}
