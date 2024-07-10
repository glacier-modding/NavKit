#pragma once

#include <direct.h>
#include <iostream>
#include <string>
#include <stdio.h>
#include "..\RecastDemo\SampleInterfaces.h"

void extractScene(BuildContext* context, char* hitmanFolder, char* sceneName, char* outputFolder);
void generateObj(BuildContext* context, char* prims, char* outputFolder);