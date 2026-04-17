The program simulates a 4 way stop intersection using a c program. Each car in the simulation is a thread, and the program uses pthreads, mutexes, condition variables, and semaphores to simulate road laws. Cars arrive at the intersection at hardcoded times, stop, and then cross based on the programmed rules. The simulation is made in a way that no two cars will collide with each other, while still allowing cars that in any other cars path to cross at the same time

running instructions:

gcc tc.c

./tc
