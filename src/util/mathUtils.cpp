#include "util/mathUtils.h"

float dsin(float deg)
{
	return std::sin(deg * 3.141592653589793f / (float)180.0f);
}
float dcos(float deg)
{
	return std::cos(deg * 3.141592653589793f / (float)180.0f);
}