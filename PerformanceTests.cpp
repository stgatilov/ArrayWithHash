#include "PerformanceTests.h"
#include "ArrayHash.h"
#include "StdMapWrapper.h"

void SpeedAll(bool compareToStl) {
#define TIME_CALL(func, params, paramsFast) { \
	if (compareToStl) { \
		double timeMine = Speed_##func<ArrayHash>params; \
		double timeStl = Speed_##func<StdMapWrapper>params; \
		printf("%s %s: %0.2lf mine, %0.2lf stl, %0.2lf speedup\n", #func, #params, timeMine, timeStl, timeStl / timeMine); \
	} \
	else { \
		double timeMine = Speed_##func<ArrayHash>paramsFast; \
		printf("%s %s: %0.2lf mine\n", #func, #paramsFast, timeMine); \
	} \
}

	TIME_CALL(GetArrayRandomHit      , (100000, 100), (100000, 1000));
	TIME_CALL(GetArrayRandomMiss     , (100000, 100), (100000, 1000));
	TIME_CALL(GetArrayRandomMix      , (100000, 100), (100000, 1000));
	TIME_CALL(GetHashRandomHit       , (100000, 100), (100000, 100));
	TIME_CALL(GetHashRandomMiss      , (100000, 100), (100000, 100));
	                                                               
	TIME_CALL(GrowthArraySequental   , (100000, 100), (100000, 1000));
	TIME_CALL(GrowthArrayRandom      , (100000, 100), (100000, 100));
	TIME_CALL(GrowthHashRandom       , (100000, 100), (100000, 100));
	                                                               
	TIME_CALL(RemoveArrayRandom      , (100000, 100), (100000, 1000));
	TIME_CALL(RemoveHashRandom       , (100000, 100), (100000, 100));
	                                                               
	TIME_CALL(SetArraySequentalMiss  , (100000, 100), (100000, 1000));
	                                                               
	TIME_CALL(GetPtrArrayRandomMix   , (100000, 100), (100000, 1000));
	TIME_CALL(SetArrayRandomMix      , (100000, 100), (100000, 1000));
	TIME_CALL(SetIfNewArrayRandomMix , (100000, 100), (100000, 1000));

#undef TIME_CALL
}
