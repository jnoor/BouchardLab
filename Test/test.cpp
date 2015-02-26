//test.cpp

#include "test.h"

Test::Test(int keyval)
{
	key = keyval;
	value = keyval * 5;
}

int Test::getValue()
{
	return value;
}
int Test::showKey()
{
	return key;
}