#include "mdump.h"

class CObject
{
public:
	BOOL mIsOpened;
};

void crash1()
{
	CObject* Object = new CObject;

	Object = NULL;
	Object->mIsOpened = false;
}

int main()
{
	CMiniDump::Begin();
	crash1();
	CMiniDump::End();
	return 0;
}