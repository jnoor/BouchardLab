#include <iostream>
#include "test.h"

int main()
{
	Test a1(5);
	std::cout << a1.getValue() << std::endl;
	std::cout << a1.showKey()  << std::endl;
	return 0;
}