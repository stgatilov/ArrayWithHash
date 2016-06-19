#include "PerformanceTests.h"
#include "ArrayWithHash.h"
#include "StdMapWrapper.h"
#include <vector>
#include <string>

template<class T> static std::string to_string(T val, const char *format) {
	char buff[256];
	sprintf(buff, format, val);
	return buff;
}

void SpeedAll(bool compareToStl) {
	std::vector<std::vector<std::string>> table;

#define TIME_CALL(func, params, paramsFast) { \
	std::vector<std::string> row; \
	if (compareToStl) { \
		double timeMine = Speed_##func<ArrayWithHash>params; \
		double timeStl = Speed_##func<StdMapWrapper>params; \
		row.push_back(#func); \
		row.push_back(#params); \
		row.push_back(to_string(timeMine, "mine: %7.2lf")); \
		row.push_back(to_string(timeStl , "stl: %7.2lf")); \
		row.push_back(to_string(timeStl / timeMine, "speedup: %5.2lf")); \
	} \
	else { \
		double timeMine = Speed_##func<ArrayWithHash>paramsFast; \
		row.push_back(#func); \
		row.push_back(#paramsFast); \
		row.push_back(to_string(timeMine, "mine: %7.2lf")); \
	} \
	for (size_t i = 0; i < row.size(); i++) \
		fprintf(stderr, "%s ", row[i].c_str()); \
	fprintf(stderr, "\n"); \
	table.push_back(row); \
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

	//print a well-aligned table to stdout
	std::vector<size_t> width(table[0].size(), 0);
	for (size_t i = 0; i < table.size(); i++)
		for (size_t j = 0; j < table[i].size(); j++)
			width[j] = std::max(width[j], table[i][j].size());
	for (size_t i = 0; i < table.size(); i++) {
		for (size_t j = 0; j < table[i].size(); j++) {
			if (j) printf(" | ");
			const auto &cell = table[i][j];
			printf("%s", cell.c_str());
			for (size_t k = 0; k < width[j] - cell.size(); k++)
				printf(" ");
		}
		printf("\n");
	}
}
