Project Overview
For this project I decided to make a cpu scheduler simulation that when given a text file of processes will simulate running either first come first serve, shortest job first, or round robin scheduling methods. This information and all program functionality is displayed in a graphical user interface where the user can run a simulation of their choosing and save their simulations to a database which they can then load simulations from. In addition a cache is used to store the most recently loaded simulation so that less time is taken when reloading a previously loaded simulation.

Themes Used
1. Seperation of concerns: The program is written into modules. This being the graphics layer, the logic layer, and the database layer. These modules seperately handle the associated graphics, logic, and database of the program.

2. CPU Scheduling: The program simulates CPU scheduling methods such as first come first serve, shortest job first, and round robin.

3. Caching: The results of the last loaded simulation are saved in a cache so that if the user wishes to retrieve the last loaded simulation the results are able to be found quickly.

4. SQL injection prevention: All queries use parameterized statements to prevent SQL injection.


Design Decisions and Trade-offs
I decided to develop the GUI and database using GTK and SQLite. This is because both are natively written in C. Doing so makes the program easier to compile. However, it is harder to develop GTK applications in Windows compared to Linux. There are some workarounds to alleviate this problem however.

Challenges Encountered and Lessons Learned
Learning how to use GTK and format the GUI was challenging.