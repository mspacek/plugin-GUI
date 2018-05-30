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
#include "NpyFile.h"

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
		void writeEvent(int eventIndex, const MidiMessage& event) override;
		void resetChannels() override;
		void addSpikeElectrode(int index, const SpikeChannel* elec) override;
		void writeSpike(int electrodeIndex, const SpikeEvent* spike) override;
		void writeTimestampSyncText(uint16 sourceID, uint16 sourceIdx, int64 timestamp, float, String text) override;
		String getMessageHeader(String datetime);
		void setParameter(EngineParameter& parameter) override;

		static RecordEngineManager* getEngineManager();

	private:

		class EventRecording
		{
		public:
			ScopedPointer<NpyFile> dataFile;
			//ScopedPointer<NpyFile> tsFile;
			//ScopedPointer<NpyFile> chanFile;
			//ScopedPointer<NpyFile> extraFile;
		};

		void increaseEventCounts(EventRecording* rec);
		static String getProcessorString(const InfoObjectCommon* channelInfo);
		String getRecordingNumberString(int recordingNumber);

		bool m_saveTTLWords{ true };
	
		HeapBlock<float> m_scaledBuffer;
		HeapBlock<int16> m_intBuffer;
		HeapBlock<int64> m_tsBuffer;
		int m_bufferSize;

		OwnedArray<SequentialBlockFile> m_DataFiles;
		Array<unsigned int> m_channelIndexes;
		Array<unsigned int> m_fileIndexes;
		EventRecording* m_dinFile;
		EventRecording* m_spikeFile;
		ScopedPointer<FileOutputStream> m_msgFile;

		//int m_recordingNum;
		Array<int64> m_startTS;

		//Compile-time constants
		const int samplesPerBlock{ 4096 };
	};

}

#endif
