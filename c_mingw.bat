g++ TestsMain.cpp CorrectnessTests.cpp PerformanceTests.cpp timer.c -O2 -std=c++11 -D NDEBUG -D AWH_TESTING -D AWH_CONTROL_INLINING -o TestsMain_mingw.exe
if errorlevel 1 exit
g++ PerformanceTests.cpp -S -O2 -std=c++11 -D NDEBUG -D AWH_TESTING -D AWH_CONTROL_INLINING
