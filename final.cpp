#include <iostream>
#include <queue>
#include <string>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cmath>
#include <mutex>
#include <condition_variable>
#include <stdio.h>
#include <memory>
#include <utility>
#include <curl/curl.h>

using namespace std;

//Holds Person Data for each line
struct Person {
    string personID;
    int startpoint = 0;
    int endpoint = 0;

    Person(string&& id, int start, int end)
        : personID(move(id)), startpoint(start), endpoint(end) {}
};

//HOLDS ELEVATOR INFORMATION FOR SCHEDULER
struct Elevator {
    string bayID;
    int lowestFloor;
    int highestFloor;
    int currentPosFloor;
    int totalCapacity;
    string direction;
};

//queue to store people for elevator
queue<unique_ptr<Person>> personQueue;
//store all elevators avilable
vector<Elevator> elevatorList;

string assEle, assPer;

int eleTotalCount = 0;

//mutexes for making sure no data leaks 
mutex elevatorMutex, personMutex;
// coordinates for asssignment of elevators
condition_variable assignmentReady, newPersonReady;

//flags for simulation, assign for elevator, remove from people queue
bool RunFlag = false;
bool ReadyFlag = false;
bool Scheduleflag = false;
int timeStep = 0;

static size_t WriteCallBack(void *contents, size_t size, size_t nmemb, void *userp) {
    ((string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

void ProcessData(const string& data) {

    if (data == "NONE") 
    {
        this_thread::sleep_for(chrono::milliseconds(1500));
        if (timeStep > 12) 
        {
            this_thread::sleep_for(chrono::milliseconds(5000));
        }
        timeStep = timeStep + 1;
    } 
    else 
    {
        istringstream line_data(data);
        string id;
        int start, end;
        getline(line_data, id, '|');

        line_data >> start;
        line_data.ignore(); 
        line_data >> end;

        unique_ptr<Person> person(new Person(move(id), start, end));
        {
            unique_lock<mutex> personlock(personMutex);
            personQueue.push(move(person));
            newPersonReady.notify_one();
            Scheduleflag = true;
        }
        this_thread::sleep_for(chrono::milliseconds(200));
        timeStep = 0;
    }
}


void InputThread() 
{
    while (RunFlag == true) 
    {
        curl_global_init(CURL_GLOBAL_ALL);
        CURL *curl = curl_easy_init();

        if (curl) 
        {
            string data;
            curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:5432/NextInput");
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallBack);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);

            CURLcode result = curl_easy_perform(curl);

            if (result == CURLE_OK) {
                if (data != "SIMULATION FAILED") {
                    ProcessData(data);
                } 
                    
                
            } else {
                
                RunFlag = false;
            }
            curl_easy_cleanup(curl);
        }
    }
    
}

int EvalElevator(const string& data, const Elevator& elevator, const Person& person, const string& personDirection) {
    //stores currentpos and passengecount
    int currentPos, passengerCount, passengerRemaining;
    string elvid, elvdirection;
    
    
    istringstream idata_line(data);

    getline(idata_line, elvid, '|');
    idata_line >> currentPos;
    idata_line.ignore();

    getline(idata_line, elvdirection, '|');
    idata_line >> passengerCount;
    idata_line.ignore();
    idata_line >> passengerRemaining;

    // Sets score as the highestfloor value so it can calculate worse case
    int score = INT_MAX; 

    if (elevator.lowestFloor <= person.startpoint && person.startpoint <= elevator.highestFloor &&
        elevator.lowestFloor <= person.endpoint && person.endpoint <= elevator.highestFloor && passengerRemaining > 0) {
        if (personDirection == "D" && currentPos >= person.startpoint) {
            if (elvdirection == "D") {
                score = (passengerRemaining % elevator.totalCapacity) + currentPos - person.startpoint;
            } else {
                string prevDir = elevator.direction;
                if (elvdirection == "S" && prevDir == "D") {
                    score = (passengerRemaining % elevator.totalCapacity) + abs(currentPos - person.startpoint);
                } else {
                    score = (passengerRemaining % elevator.totalCapacity) + (elevator.highestFloor - currentPos) * 2 + (currentPos - person.startpoint);
                }
            }
        } else if (personDirection == "U" && currentPos <= person.startpoint) {
            if (elvdirection == "U") {
                score = (passengerRemaining % elevator.totalCapacity) + person.startpoint - currentPos;
            } else {
                string prevDir = elevator.direction;
                if (elvdirection == "S" && prevDir == "U") {
                    score = (passengerRemaining % elevator.totalCapacity) + person.startpoint - currentPos;
                } else {
                    score = (passengerRemaining % elevator.totalCapacity) + (currentPos - elevator.lowestFloor) * 2 + (person.startpoint - currentPos);
                }
            }
        } else {
            if (elvdirection == "D") {
                score = (passengerRemaining % elevator.totalCapacity) + currentPos - elevator.lowestFloor + person.startpoint - elevator.lowestFloor;
            } else {
                string prevDir = elevator.direction;
                if (elvdirection == "S" && prevDir == "D") {
                    score = (passengerRemaining % elevator.totalCapacity) + currentPos - elevator.lowestFloor + person.startpoint - elevator.lowestFloor;
                } else {
                    score = (passengerRemaining % elevator.totalCapacity) + elevator.highestFloor - currentPos + elevator.highestFloor + person.startpoint - elevator.lowestFloor;
                }
            }
        }
    }

    return score;
}

string ElevatorStatus(const string& url) {
    CURLcode result;
    string data;
    CURL *curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallBack);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);

        result = curl_easy_perform(curl);
        if (result != CURLE_OK) {
            cerr << "Curl Error" << endl;
            data = "";  
        }
        curl_easy_cleanup(curl);
    }
    return data;
}

void ScheduleThread() {
    CURL *curl;
    CURLcode result;

    while (RunFlag == true) {
        while (!personQueue.empty()) {
            unique_ptr<Person> next;
            {
                unique_lock<mutex> personlock(personMutex);
                newPersonReady.wait(personlock, [] {return Scheduleflag;});
                next = move(personQueue.front());
                personQueue.pop();
            }

            Scheduleflag = false;
            string personDirection = (next->startpoint < next->endpoint) ? "U" : "D";
            int bestScore = 100;
            string scoreElevator;

            for (int i = 0; i < eleTotalCount; i++) {
                stringstream line_data;
                line_data << "http://localhost:5432/ElevatorStatus/" << elevatorList[i].bayID;
                string url = line_data.str();
    
                string data = ElevatorStatus(url);
                if (!data.empty()) {
                    int score = EvalElevator(data, elevatorList[i], *next, personDirection);
                    if (score < bestScore) {
                        bestScore = score;
                        scoreElevator = elevatorList[i].bayID;
                    }
                }
            }

            if (!scoreElevator.empty()) {
                unique_lock<mutex> lock1(elevatorMutex);
                assPer = next->personID;
                assEle = scoreElevator;
                ReadyFlag = true;
                assignmentReady.notify_one();
                cout << assPer << " Is on an Elevator " << scoreElevator << endl;
            }
        }
    }
    cout << "Scheduler Finished" << endl;
}


void OutputThread() {
    CURL *curl;
    CURLcode result;

    while (RunFlag == true) 
    {
        while(personQueue.size() >0)
        {
            unique_lock<mutex> elevatorlock(elevatorMutex);
            assignmentReady.wait(elevatorlock, [] { return ReadyFlag; });

            stringstream line_data;
            line_data << "http://localhost:5432/AddPersonToElevator/";
            line_data << assPer;
            line_data << "/";
            line_data << assEle;
            string url = line_data.str();

            ReadyFlag = false;
    
            curl = curl_easy_init();

            if (curl) 
            {
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");

                result = curl_easy_perform(curl);

                // DONT NEED THIS CHECK
                if (result != CURLE_OK) {
                    cerr << "Curl Error" << endl;
                } else {
                    cout << assPer << "in queue" << endl;
                }
                curl_easy_cleanup(curl);
            }
            elevatorlock.unlock();
        }
    }
}
void SimulationControl(const string& command) {
    CURL *curl;
    CURLcode result;
    curl = curl_easy_init();
    if (curl) {
        string url = "http://localhost:5432/Simulation/" + command;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");

        result = curl_easy_perform(curl);
        if (result != CURLE_OK) {
            cerr << "Curl Error" << command << endl;
        } else if (command == "start") {
            RunFlag = true;
        }
        curl_easy_cleanup(curl);
    }
}



void initialize_elevators(ifstream& inputFile) {
    string line;
    while (getline(inputFile, line)) {
        stringstream line_data(line);
        int lowestfloor, highestfloor, currentfloor, totalcapacity;
        string bayID;
        getline(line_data, bayID, '\t');
        line_data >> lowestfloor >> highestfloor >> currentfloor >> totalcapacity;
        elevatorList.push_back({bayID, lowestfloor, highestfloor, currentfloor, totalcapacity, "S"});
    }
    eleTotalCount = elevatorList.size();
}

int main(int argc, char *argv[]) {
    
    if (argc !=2) {
        cout << "Usage: " << argv[0] << " <filename> " << endl;
        return 1;
    }
    ifstream inputFile(argv[1]);
    if (!inputFile.is_open()) {
        cout << "Error: Unable to open file: " << argv[1] << endl;
        return 1;
    }


    initialize_elevators(inputFile);
    inputFile.close();


    SimulationControl("start");    

    thread inputthread(InputThread);
    thread schedulethread(ScheduleThread);
    thread outputthread(OutputThread);
    
    inputthread.join();
    schedulethread.join();
    outputthread.join();
    
    SimulationControl("end");    
    return 0;
}

