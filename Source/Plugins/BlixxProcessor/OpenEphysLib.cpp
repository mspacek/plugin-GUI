/*
------------------------------------------------------------------

This file is part of the Open Ephys GUI
Copyright (C) 2013 Open Ephys

------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <PluginInfo.h>
#include "BlixxProcessor.h"
#include <string>
#ifdef WIN32
#include <Windows.h>
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

using namespace Plugin;
/* Number of plugins defined by the library. Can be of different types
 * (Processors, RecordEngines, etc...) */
#define NUM_PLUGINS 1

extern "C" EXPORT void getLibInfo(Plugin::LibraryInfo* info)
{
    /*API version, defined by the GUI source. Should not be changed to ensure it is always
     * equal to the one used in the latest codebase. The GUI refueses to load plugins with
     * mismatched API versions */
    info->apiVersion = PLUGIN_API_VER;
    info->name = "Blixx Processor"; // Name of the Library, used only for information
    info->libVersion = 1; // Version of the library, used only for information
    info->numPlugins = NUM_PLUGINS;
}

extern "C" EXPORT int getPluginInfo(int index, Plugin::PluginInfo* info)
{
    switch (index)
    {
    case 0:
        info->type = Plugin::PLUGIN_TYPE_PROCESSOR;
        info->processor.name = "Blixx Processor"; // Processor name shown in the GUI
        // Type of processor: FilterProcessor, SourceProcessor, SinkProcessor, UtilityProcessor
        info->processor.type = Plugin::FilterProcessor;
        info->processor.creator = &(Plugin::createProcessor<BlixxProcessor>); // Class factory pointer
        break;
/**
Examples for other plugin types

For a RecordEngine, which provides formats for recording data
    case x:
        info->type = Plugin::RecordEnginePlugin;
        info->recordEngine.name = "Record Engine Name";
        info->recordEngine.creator = &(Plugin::createRecordEngine<RecordEngineClassName>);
        break;

For a DataThread, which allows to use the existing SourceNode to connect to an asynchronous data source, such as acquisition hardware
    case x:
        info->type = Plugin::DatathreadPlugin;
        info->dataThread.name = "Source name"; //Name that will appear on the processor list
        info->dataThread.creator = &createDataThread<DataThreadClassName>;

For a FileSource, which allows importing data formats into the FileReader
    case x:
        info->type = Plugin::FileSourcePlugin;
        info->fileSource.name = "File Source Name";
        info->fileSource.extensions = "xxx;xxx;xxx"; //Semicolon separated list of supported extensions. Eg: "txt;dat;info;kwd"
        info->fileSource.creator = &(Plugin::createFileSource<FileSourceClassName>);
**/
    default:
        return -1;
        break;
    }
    return 0;
}

#ifdef WIN32
BOOL WINAPI DllMain(IN HINSTANCE hDllHandle,
    IN DWORD     nReason,
    IN LPVOID    Reserved)
{
    return TRUE;
}

#endif