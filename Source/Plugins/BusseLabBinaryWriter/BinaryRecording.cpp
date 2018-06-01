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

#include "BinaryRecording.h"

#define MAX_BUFFER_SIZE 40960

using namespace BinaryRecordingEngine;

BinaryRecording::BinaryRecording()
{
	m_scaledBuffer.malloc(MAX_BUFFER_SIZE);
	m_intBuffer.malloc(MAX_BUFFER_SIZE);
}

BinaryRecording::~BinaryRecording()
{

}

String BinaryRecording::getEngineID() const
{
	return "BUSSELABRAWBINARY";
}

String BinaryRecording::getProcessorString(const InfoObjectCommon* channelInfo)
{
	String fName = (channelInfo->getCurrentNodeName().replaceCharacter(' ', '_') + "-" +
					String(channelInfo->getCurrentNodeID()));
	if (channelInfo->getCurrentNodeID() == channelInfo->getSourceNodeID())
	// "it" is the channel source - mspacek doesn't know what this means...
	{
		fName += "." + String(channelInfo->getSubProcessorIdx());
	}
	else
	{
		fName += "_" + String(channelInfo->getSourceNodeID()) + "." +
				 String(channelInfo->getSubProcessorIdx());
	}
	fName += File::separatorString;
	return fName;
}

String BinaryRecording::getRecordingNumberString(int recordingNumber)
{
	String s = "";
	if (recordingNumber > 0)
		s = "_r" + String(recordingNumber).paddedLeft('0', 2); // pad with at most 1 leading 0
	return s;
}

/*

TODO:

* test spike detection and saving
* check assumption that there's only one spike detector in the signal chain?
* how does clustering work? does it fill the cluster id field in .spikes.npy properly?
* get channel map working
* parse the chanmap to extract probe_name
* need to deal with 0 vs 1-based channel IDs in .json and spike.npy - which to use depends on probe type
* test that enabled chans are written correctly to .json when some chans are disabled
* handle auxchans, if any, in .json
* add git rev to .json/.msg.txt?
*/

void BinaryRecording::openFiles(File rootFolder, String baseName, int recordingNumber)
{
	String basepath = rootFolder.getFullPathName() + rootFolder.separatorString + baseName;

	int nProcessors = getNumRecordedProcessors();
	if (nProcessors != 1)
		std::cerr << "ERROR: BusseLabBinaryWriter plugin assumes only 1 processor, found "
				  << nProcessors << std::endl;

	// collect some channel parameters:
	int nChans = getNumRecordedChannels();
	std::cout << "Number of recorded channels: " << nChans << std::endl;
	m_channelIndexes.insertMultiple(0, 0, nChans);
	m_fileIndexes.insertMultiple(0, 0, nChans);

	const RecordProcessorInfo& pInfo0 = getProcessorInfo(0); // info for processor 0
	int nRecChans = pInfo0.recordedChannels.size();
	int recordedChan0 = pInfo0.recordedChannels[0];
	int realChan0 = getRealChannel(recordedChan0);
	const DataChannel* channelInfo0 = getDataChannel(realChan0);

	// compare each chan's sample rate and uV per AD to that of chani 0:
	int sample_rate = channelInfo0->getSampleRate();
	double uV_per_AD = channelInfo0->getBitVolts();

	// iterate over all recorded chans:
	var chans; // holds enabled chans
	for (int chani = 0; chani < nRecChans; chani++)
	{
		int recordedChan = pInfo0.recordedChannels[chani];
		chans.append(recordedChan);
		int realChan = getRealChannel(recordedChan);
		const DataChannel* channelInfo = getDataChannel(realChan);

		// compare to chani 0:
		if (channelInfo->getSampleRate() != sample_rate)
			std::cerr << "ERROR: sample rate of chan " << realChan << " != " << sample_rate
					  << std::endl;
		if (channelInfo->getBitVolts() != uV_per_AD)
			std::cerr << "ERROR: uV_per_AD of chan " << realChan << " != " << uV_per_AD
					  << std::endl;

		// fill in m_channelIndexes and m_fileIndexes for use in writeData, though
		// given the simplified setup assumed, these might not be necessary any more:
		/// TODO: is this right? shouldn't the args be reversed??????????????
		m_channelIndexes.set(recordedChan, chani); // index, value
		m_fileIndexes.set(recordedChan, 0);
	}

	// open .dat file:
	String datFileName = basepath;
	datFileName += getRecordingNumberString(recordingNumber) + ".dat";
	ScopedPointer<SequentialBlockFile> bFile = new SequentialBlockFile(nRecChans,
																	   samplesPerBlock);
	std::cout << "OPENING FILE: " << datFileName << std::endl;
	if (bFile->openFile(datFileName))
		m_DataFiles.add(bFile.release());
	else
		m_DataFiles.add(nullptr);

	// get start timestamps for all channels (TODO: should be the same for all?):
	int nsamples_offset = getTimestamp(0);
	for (int i = 0; i < nChans; i++)
	{
		if (i == 0)
			std::cout << "Start timestamp: " << nsamples_offset << std::endl;
		jassert(getTimestamp(i) == nsamples_offset);
		m_startTS.add(getTimestamp(i));
	}
	Time now = Time::getCurrentTime();
	String datetime = now.toISO8601(true);
	String tz = now.getUTCOffsetString(true);
	datetime = datetime.upToLastOccurrenceOf(tz, false, false); // strip time zone
	//datetime = datetime.upToLastOccurrenceOf(".", false, false); // strip subseconds

	// collect .dat metadata in JSON data structure:
	DynamicObject::Ptr json = new DynamicObject();
	//json->setProperty("dat_fname", datFileName);
	json->setProperty("nchans", nRecChans);
	json->setProperty("sample_rate", sample_rate);
	json->setProperty("dtype", "int16");
	json->setProperty("uV_per_AD", uV_per_AD);
	/// TODO: parse the chanmap to extract probe_name:
	json->setProperty("probe_name", "");
	json->setProperty("chans", chans);
	/// TODO: handle auxchans, if any
	var auxchans;
	if (auxchans)
		json->setProperty("auxchans", auxchans);
	json->setProperty("nsamples_offset", nsamples_offset);
	json->setProperty("datetime", datetime);
	json->setProperty("author", "Open-Ephys, BusseLabBinaryWriter plugin");
	String version = CoreServices::getGUIVersion() + ", " + BusseLabBinaryWriterPluginVersion;
	json->setProperty("version", version);
	json->setProperty("notes", "");

	// write .dat metadata to .dat.json file:
	String jsonFileName = basepath;
	jsonFileName += getRecordingNumberString(recordingNumber) + ".dat.json";
	File jsonf = File(jsonFileName);
	Result res = jsonf.create();
	if (res.failed())
		std::cerr << "Error creating JSON file:" << res.getErrorMessage()
				  << std::endl;
	ScopedPointer<FileOutputStream> jsonFile = jsonf.createOutputStream();
	String jsonstr = JSON::toString(var(json), false); // JUCE 5.3.2 has maximumDecimalPlaces
	std::cout << "WRITING FILE: " << jsonFileName << std::endl;
	jsonFile->writeText(jsonstr, false, false, nullptr);
	jsonFile->flush();

	// open .msg.txt and .din.npy event files:
	int nEventChans = getNumRecordedEventChannels();
	for (int evChani = 0; evChani < nEventChans; evChani++)
	{
		const EventChannel* chan = getEventChannel(evChani);

		switch (chan->getChannelType())
		{
		case EventChannel::TEXT:
			{
				String msgFileName = basepath;
				msgFileName += getRecordingNumberString(recordingNumber) + ".msg.txt";
				std::cout << "OPENING FILE: " << msgFileName << std::endl;
				File msgf = File(msgFileName);
				Result res = msgf.create();
				if (res.failed())
					std::cerr << "Error creating message text file:" << res.getErrorMessage()
							  << std::endl;
				else
					m_msgFile = msgf.createOutputStream(); // store file handle
					m_msgFile->writeText(getMessageHeader(datetime), false, false, nullptr);
					m_msgFile->flush();
				break;
			}
		case EventChannel::TTL:
			{
				if (!m_saveTTLWords)
					break;
				String dinFileName = basepath;
				dinFileName += getRecordingNumberString(recordingNumber) + ".din.npy";
				std::cout << "OPENING FILE: " << dinFileName << std::endl;
				ScopedPointer<EventRecording> rec = new EventRecording();
				// 2D, each row is [timestamp, word]:
				NpyType dindtype = NpyType(BaseType::INT64, 2);
				rec->dataFile = new NpyFile(dinFileName, dindtype);
				m_dinFile = rec.release(); // store pointer to rec object
				break;
			}
		}
	}

	// open .spikes.npy file:
	String spikeFileName = basepath;
	spikeFileName += getRecordingNumberString(recordingNumber) + ".spikes.npy";
	std::cout << "OPENING FILE: " << spikeFileName << std::endl;
	ScopedPointer<EventRecording> rec = new EventRecording();
	// 3D, each row is [timestamp, chani, clusteri]:
	NpyType spikedtype = NpyType(BaseType::INT64, 3);
	rec->dataFile = new NpyFile(spikeFileName, spikedtype);
	m_spikeFile = rec.release(); // store pointer to rec object

	//m_recordingNum = recordingNumber; // don't really need to store this?
}


template <typename TO, typename FROM>
void dataToVar(var& dataTo, const void* dataFrom, int length)
{
	const FROM* buffer = reinterpret_cast<const FROM*>(dataFrom);
	for (int i = 0; i < length; i++)
	{
		dataTo.append(static_cast<TO>(*(buffer + i)));
	}
}


void BinaryRecording::closeFiles()
{
	resetChannels();
}

void BinaryRecording::resetChannels()
{
	// dereference all stored objects, including open file handles?
	m_DataFiles.clear();
	m_channelIndexes.clear();
	m_fileIndexes.clear();
	m_dinFile = nullptr;
	m_spikeFile = nullptr;
	m_msgFile = nullptr;

	m_scaledBuffer.malloc(MAX_BUFFER_SIZE);
	m_intBuffer.malloc(MAX_BUFFER_SIZE);
	m_bufferSize = MAX_BUFFER_SIZE;
	m_startTS.clear();
}

void BinaryRecording::writeData(int writeChannel, int realChannel, const float* buffer,
								int size)
{
	if (size > m_bufferSize)
	// shouldn't happen, and if it does it'll be slow, but better this than crashing
	{
		std::cerr << "Write buffer overrun, resizing to" << size << std::endl;
		m_bufferSize = size;
		m_scaledBuffer.malloc(size);
		m_intBuffer.malloc(size);
	}
	double multFactor = 1 / (float(0x7fff) * getDataChannel(realChannel)->getBitVolts());
	FloatVectorOperations::copyWithMultiply(m_scaledBuffer.getData(), buffer, multFactor,
											size);
	AudioDataConverters::convertFloatToInt16LE(m_scaledBuffer.getData(), m_intBuffer.getData(),
											   size);
	int fileIndex = m_fileIndexes[writeChannel];
	m_DataFiles[fileIndex]->writeChannel(getTimestamp(writeChannel) - m_startTS[writeChannel],
										 m_channelIndexes[writeChannel],
										 m_intBuffer.getData(), size);
}


void BinaryRecording::addSpikeElectrode(int index, const SpikeChannel* elec)
{
}

void BinaryRecording::writeEvent(int eventIndex, const MidiMessage& event)
{
	EventPtr ev = Event::deserializeFromMessage(event, getEventChannel(eventIndex));
	EventRecording* rec = m_dinFile;
	if (!rec)
		return;
	const EventChannel* info = getEventChannel(eventIndex);
	int64 ts = ev->getTimestamp();
	if (ev->getEventType() == EventChannel::TEXT)
	{
		const String tsstr = String(ts);
		//String msg = String((char*)ev->getRawDataPointer(), info->getDataSize());
		const String msg = String((char*)ev->getRawDataPointer());
		m_msgFile->writeText(tsstr + "\t" + msg + '\n', false, false, nullptr);
		m_msgFile->flush();
	}
	else if (ev->getEventType() == EventChannel::TTL)
	{
		TTLEvent* ttl = static_cast<TTLEvent*>(ev.get());
		// cast void pointer to uint8 pointer, dereference, cast to int64:
		int64 word = (int64)*(uint8*)(ttl->getTTLWordPointer());
		rec->dataFile->writeData(&ts, sizeof(int64)); // timestamp
		rec->dataFile->writeData(&word, sizeof(int64)); // digital input word
		increaseEventCounts(rec);
	}
	else
	{
		std::cerr << "Error. Don't know how to handle event type " << ev->getEventType()
				  << std::endl;
	}
}

String BinaryRecording::getMessageHeader(String datetime)
{
	String header = "## Generated by Open-Ephys " + CoreServices::getGUIVersion()
					+ ", BusseLabBinaryWriter plugin " + BusseLabBinaryWriterPluginVersion
					+ "\n";
	header += "## " + datetime + "\n";
	header += "## Message log format: samplei <TAB> message\n";
	header += "## Processor start time is index of first sample in .dat file\n";
	return header;
}

void BinaryRecording::writeTimestampSyncText(uint16 sourceID, uint16 sourceIdx,
											 int64 timestamp, float, String text)
{
	if (!m_msgFile)
		return;
	m_msgFile->writeText("## " + text + "\n", false, false, nullptr);
	m_msgFile->flush();
}

void BinaryRecording::writeSpike(int electrodeIndex, const SpikeEvent* spike)
{
	//std::cout << "electrodeIndex " << electrodeIndex << std::endl;
	const SpikeChannel* channel = getSpikeChannel(electrodeIndex);
	EventRecording* rec = m_spikeFile;

	int64 ts = spike->getTimestamp();
	/// why can't i do something like spike->getChannel?
	//int64 spikeChannel = (int64)(uint16)(m_spikeChannelIndexes[electrodeIndex]);
	/// TODO: electrodeIndex probably needs to be dereferenced...
	int64 spikeChannel = electrodeIndex;
	int64 sortedID = (int64)(uint16)spike->getSortedID();
	rec->dataFile->writeData(&ts, sizeof(int64)); // timestamp
	rec->dataFile->writeData(&spikeChannel, sizeof(int64)); // spike channel
	rec->dataFile->writeData(&sortedID, sizeof(int64)); // cluster ID
	increaseEventCounts(rec);
}

void BinaryRecording::increaseEventCounts(EventRecording* rec)
{
	rec->dataFile->increaseRecordCount();
	//if (rec->tsFile) rec->tsFile->increaseRecordCount();
	//if (rec->extraFile) rec->extraFile->increaseRecordCount();
	//if (rec->chanFile) rec->chanFile->increaseRecordCount();
}

RecordEngineManager* BinaryRecording::getEngineManager()
{
	RecordEngineManager* man = new RecordEngineManager("BUSSELABRAWBINARY", "Binary",
													   &(engineFactory<BinaryRecording>));
	EngineParameter* param;
	param = new EngineParameter(EngineParameter::BOOL, 0, "Record TTL full words", true);
	man->addParameter(param);
	return man;
}

void BinaryRecording::setParameter(EngineParameter& parameter)
{
	boolParameter(0, m_saveTTLWords);
}
