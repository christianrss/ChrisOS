#include "graphics.h"

int start() {
    VBEInfoBlock* VBE = (VBEInfoBlock*) VBEInfoAddress;

    mx = VBE->x_resolution / 2;
    my = VBE->y_resolution / 2;

    char characterBuffer[1000] = "\0";
    char* characterBufferPointer = characterBuffer;
    int characterBufferLength = 0;

    base = (unsigned int) &isr1;
    base12 = (unsigned int) &isr12;

    InitialiseMouse();
    InitialiseIDT();

    tasks[TasksLength].priority = 0;
    tasks[TasksLength].function = &ClearScreenTask;
    TasksLength++;

    tasks[TasksLength].priority = 0;
    tasks[TasksLength].function = &WelcomeTask;
    TasksLength++;

    tasks[TasksLength].priority = 0;
    tasks[TasksLength].taskId = TasksLength;
    tasks[TasksLength].function = &TestGraphicalElementsTask;
    iparams[TasksLength * task_params_length + 0] = 10;
    iparams[TasksLength * task_params_length + 1] = 10;
    iparams[TasksLength * task_params_length + 2] = 300;
    iparams[TasksLength * task_params_length + 3] = 300;
    TasksLength++;

    tasks[TasksLength].priority = 0;
    tasks[TasksLength].function = &HandleKeyboardTask;
    TasksLength++;

    tasks[TasksLength].priority = 0;
    tasks[TasksLength].function = &DrawMouseTask;
    TasksLength++;

    while(1) {
        ProcessTasks();

        Flush();
    }
}