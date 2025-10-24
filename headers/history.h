#pragma once

#include <vector>
#include <fstream>
#include <iostream>

#include "tab.h"

#include <stddef.h>
#include <semaphore.h>

#define MAX_CMD_LENGTH 256
#define MAX_HISTORY_SIZE 1000
#define SHM_NAME "/myterm_shm_history"
#define SEM_NAME "/myterm_sem_history"

using namespace std;

typedef struct
{
    int count;
    char commands[MAX_HISTORY_SIZE][MAX_CMD_LENGTH];
} SharedHistory;

extern sem_t *Shared_History_Semaphore;
extern SharedHistory *Shared_History_Data;

void Init_Shared_Memory();
void Attach_Shared_Memory();
void Detach_Shared_Memory();
void Cleanup_Shared_Memory();
void Load_History_From_File();
void Load_Tab_History(Tab &, int);
void Update_Shared_History(const string &);

void Init_Shared_Memory()
{
    Shared_History_Semaphore = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (Shared_History_Semaphore == SEM_FAILED)
    {
        perror("SEM_OPEN FAILED");
    }

    int Shared_Memory_FD = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (Shared_Memory_FD == -1)
    {
        perror("SHM_OPEN Failed");
    }

    if (ftruncate(Shared_Memory_FD, sizeof(SharedHistory)) == -1)
    {
        perror("FTRUNCATE Failed");
    }

    Shared_History_Data = (SharedHistory *)mmap(NULL, sizeof(SharedHistory), PROT_READ | PROT_WRITE, MAP_SHARED, Shared_Memory_FD, 0);
    if (Shared_History_Data == MAP_FAILED)
    {
        perror("MMAP Failed");
    }
    close(Shared_Memory_FD);

    if (Shared_History_Data != MAP_FAILED && Shared_History_Semaphore != SEM_FAILED)
    {
        sem_wait(Shared_History_Semaphore);
        Shared_History_Data->count = 0;
        sem_post(Shared_History_Semaphore);
    }
}

void Attach_Shared_Memory()
{
    Shared_History_Semaphore = sem_open(SEM_NAME, 0);
    if (Shared_History_Semaphore == SEM_FAILED)
        return;

    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1)
    {
        sem_close(Shared_History_Semaphore);
        Shared_History_Semaphore = SEM_FAILED;
        return;
    }

    Shared_History_Data = (SharedHistory *)mmap(NULL, sizeof(SharedHistory), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (Shared_History_Data == MAP_FAILED)
    {
        sem_close(Shared_History_Semaphore);
        Shared_History_Semaphore = SEM_FAILED;
    }

    close(shm_fd);
}

void Detach_Shared_Memory()
{
    if (Shared_History_Data && Shared_History_Data != MAP_FAILED)
        munmap(Shared_History_Data, sizeof(SharedHistory));
    if (Shared_History_Semaphore && Shared_History_Semaphore != SEM_FAILED)
        sem_close(Shared_History_Semaphore);
}

void Cleanup_Shared_Memory()
{
    if (Shared_History_Data != MAP_FAILED)
    {
        munmap(Shared_History_Data, sizeof(SharedHistory));
        Shared_History_Data = (SharedHistory *)MAP_FAILED;
    }
    shm_unlink(SHM_NAME);

    if (Shared_History_Semaphore != SEM_FAILED)
    {
        sem_close(Shared_History_Semaphore);
        Shared_History_Semaphore = SEM_FAILED;
    }
    sem_unlink(SEM_NAME);
}

void Load_History_From_File()
{
    if (!Shared_History_Data || Shared_History_Data == (SharedHistory *)MAP_FAILED || Shared_History_Semaphore == SEM_FAILED)
        return;

    string line;
    vector<string> T_History;

    fstream History_File(HISTORY_FILE_NAME, ios::in | ios::app);
    if (!History_File.is_open())
    {
        std::cerr << "Error: Could not open file: " << HISTORY_FILE_NAME << std::endl;
        return;
    }
    while (getline(History_File, line))
    {
        if (!line.empty())
        {
            T_History.push_back(line);
        }
    }
    History_File.close();

    sem_wait(Shared_History_Semaphore);
    int start_index = max(0, (int)T_History.size() - (int)MAX_HISTORY_SIZE);
    int history_index = 0;

    for (size_t i = start_index; i < T_History.size(); ++i)
    {
        string &command = T_History[i];
        strncpy(Shared_History_Data->commands[history_index], command.c_str(), MAX_CMD_LENGTH - 1);
        Shared_History_Data->commands[history_index][MAX_CMD_LENGTH - 1] = '\0';
        history_index++;
    }

    Shared_History_Data->count = history_index;
    sem_post(Shared_History_Semaphore);
}

void Load_Tab_History(Tab &T, int delta)
{
    if (!Shared_History_Data || Shared_History_Data == (SharedHistory *)MAP_FAILED || Shared_History_Semaphore == SEM_FAILED)
        return;

    sem_wait(Shared_History_Semaphore);
    int count = Shared_History_Data->count;
    sem_post(Shared_History_Semaphore);

    if (count == 0)
    {
        T.history_index = -1;
        return;
    }
    if (T.history_index == -1)
    {
        T.history_index = 0;
    }

    T.history_index += delta;
    if (T.history_index < 0)
        T.history_index = 0;
    if (T.history_index >= count)
        T.history_index = count - 1;

    int Array_Index = count - 1 - T.history_index;

    T.line = Shared_History_Data->commands[Array_Index];
    T.writing_index = T.line.size();
}

void Update_Shared_History(string &command)
{
    if (command.empty())
        return;
    if (!Shared_History_Data || Shared_History_Data == (SharedHistory *)MAP_FAILED || Shared_History_Semaphore == SEM_FAILED)
    {
        cout << "FAILED HERE" << endl;
        return;
    }

    fstream History_File(HISTORY_FILE_NAME, ios::out | ios::app);
    if (History_File.is_open())
    {
        History_File << command << endl;
        History_File.close();
    }
    else
    {
        cerr << "Warning: Could not open history file for writing: " << endl;
    }

    sem_wait(Shared_History_Semaphore);

    int count = Shared_History_Data->count;
    if (count == MAX_HISTORY_SIZE)
    {
        for (int i = 0; i < MAX_HISTORY_SIZE - 1; ++i)
        {
            strncpy(Shared_History_Data->commands[i], Shared_History_Data->commands[i + 1], MAX_CMD_LENGTH);
            Shared_History_Data->commands[i][MAX_CMD_LENGTH - 1] = '\0';
        }
        strncpy(Shared_History_Data->commands[MAX_HISTORY_SIZE - 1], command.c_str(), MAX_CMD_LENGTH - 1);
        Shared_History_Data->commands[MAX_HISTORY_SIZE - 1][MAX_CMD_LENGTH - 1] = '\0';
    }
    else
    {
        strncpy(Shared_History_Data->commands[count], command.c_str(), MAX_CMD_LENGTH - 1);
        Shared_History_Data->commands[count][MAX_CMD_LENGTH - 1] = '\0';
        Shared_History_Data->count++;
    }

    sem_post(Shared_History_Semaphore);
}