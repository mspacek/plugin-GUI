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
#ifndef BINARYRECORDING_H
#define BINARYRECORDING_H

#include <RecordingLib.h>
#include "SequentialBlockFile.h"

//Defines for the old-stype original recording spikes and event files
#define HEADER_SIZE 1024
#define BLOCK_LENGTH 1024
#define VERSION 0.4
#define VSTR(s) #s
#define VSTR2(s) VSTR(s)
#define VERSION_STRING VSTR2(VERSION)

namespace BinaryRecordingEngine
{

    class BinaryRecording : public RecordEngine
    {
    public:
        BinaryRecording();
        ~BinaryRecording();

        String getEngineID() const override;
        void openFiles(File rootFolder, String baseName, int recordingNumber) override;
        void closeFiles() override;
        void writeData(int writeChannel, int realChannel, const float* buffer, int size) override;
        void writeEvent(int eventType, const MidiMessage& event, int64 timestamp) override;
        void resetChannels() override;
        void addSpikeElectrode(int index, const SpikeRecordInfo* elec) override;
        void writeSpike(int electrodeIndex, const SpikeObject& spike, int64 timestamp) override;

        static RecordEngineManager* getEngineManager();

    private:

        String generateSpikeHeader(SpikeRecordInfo* elec);
        String generateEventHeader();

        void openDINFile(String basepath, int recordingNumber);
        void openEventFile(String basepath, int recordingNumber);
        void openSpikeFile(String basepath, SpikeRecordInfo* elec, int recordingNumber);
        void openMessageFile(String basepath, int recordingNumber);
        // Saves (int64 timestamp, uint64 TTL word) pairs to .din file on every bit change:
        void writeTTLEvent(const MidiMessage& event, int64 timestamp);
        void writeMessage(const MidiMessage& event, int64 timestamp);

        HeapBlock<float> m_scaledBuffer;
        HeapBlock<int16> m_intBuffer;
        int m_bufferSize;

        OwnedArray<SequentialBlockFile>  m_DataFiles;

        FILE* DINFile;
        FILE* eventFile;
        Array<FILE*> spikeFileArray;
        FILE* messageFile;
        int m_recordingNumber;
        Array<uint64> m_startTS;

        CriticalSection diskWriteLock;

        //Compile-time constants
        const int samplesPerBlock{ 4096 };

    };

}

#endif
