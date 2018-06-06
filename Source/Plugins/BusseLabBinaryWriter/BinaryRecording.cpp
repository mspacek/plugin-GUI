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

/*

TODO:

* test spike detection and saving
* check assumption that there's only one spike detector in the signal chain?
* how does clustering work? does it fill the cluster id field in .spikes.npy properly?
* get channel map working
* make chanmap processor update its title when loading a .prb (JSON) file, maybe store name of file, to use as probe_name
* parse the chanmap to extract probe_name, and also channel ID base
* make sure chanmap layout doesn't affect chan ordering in .dat file! only for display
* need to deal with 0 vs 1-based channel IDs in .json and spike.npy - which to use depends on probe type
* chan labelling is not preserved between blocks in signal chain, see https://open-ephys.atlassian.net/wiki/spaces/OEW/pages/950421/Channel+Map
* test that enabled chans are written correctly to .json when some chans are disabled
* consider overhauling lfpviewer so that channel order and enable/disable status is reflected
* add git rev to .json/.msg.txt?
* get "Error in Rhd2000EvalBoard::readDataBlock: Incorrect header." errors randomly, won't exit

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

void BinaryRecording::openFiles(File rootFolder, String baseName, int recordingNumber)
{
	String basepath = rootFolder.getFullPathName() + rootFolder.separatorString + baseName;

	int nRecProcessors = getNumRecordedProcessors();
	if (nRecProcessors != 1)
		std::cerr << "ERROR: BusseLabBinaryWriter plugin assumes only 1 recorded processor, "
				  << "found " << nRecProcessors << std::endl;

	// collect some channel parameters:
	int nRecChans = getNumRecordedChannels(); // num recorded channels
	int nHeadstageChans = getNumHeadstageChannels(); // number of headstage chans
	std::cout << "getNumRecordedChannels: " << nRecChans << std::endl;
	std::cout << "getNumHeadstageChannels: " << nHeadstageChans << std::endl;
	m_channelIndexes.insertMultiple(0, 0, nRecChans);
	m_fileIndexes.insertMultiple(0, 0, nRecChans);

	const RecordProcessorInfo& pInfo0 = getProcessorInfo(0); // info for processor 0
	int recordedChan0 = pInfo0.recordedChannels[0];
	int realChan0 = getRealChannel(recordedChan0);
	const DataChannel* datachan0 = getDataChannel(realChan0);

	// compare each chan's sample rate and uV per AD to that of chani 0:
	int sample_rate = datachan0->getSampleRate();
	double uV_per_AD = datachan0->getBitVolts();

	// iterate over all chans enabled for recording in processor 0:
	var chans;
	var chanNames;
	for (int chani = 0; chani < nRecChans; chani++)
	{
		int recordedChan = pInfo0.recordedChannels[chani];
		/// TODO: what's the difference between recordedChan and realChan?
		//chans.append(recordedChan);
		int realChan = getRealChannel(recordedChan); // does some kind of dereferencing?
		const DataChannel* datachan = getDataChannel(realChan);
		chans.append(realChan);
		chanNames.append(datachan->getName());

		// some diagnostics:
		//std::cout << "chan getName: " << datachan->getName() << std::endl;
		//std::cout << "chan getSourceNodeID: " << datachan->getSourceNodeID() << std::endl;
		//std::cout << "chan getSubProcessorIdx: " << datachan->getSubProcessorIdx()
				  //<< std::endl;
		//std::cout << "chan getSourceIndex: " << datachan->getSourceIndex() << std::endl;
		//std::cout << "chan getDescription: " << datachan->getDescription() << std::endl;

		// compare to chani 0:
		if (datachan->getSampleRate() != sample_rate)
			std::cerr << "ERROR: sample rate of chan " << realChan << " != " << sample_rate
					  << std::endl;
		if (datachan->getBitVolts() != uV_per_AD)
			std::cerr << "ERROR: uV_per_AD of chan " << realChan << " != " << uV_per_AD
					  << std::endl;

		// fill in m_channelIndexes and m_fileIndexes for use in writeData, though
		// given the simplified setup assumed, these might not be necessary any more:
		/// TODO: is this right? shouldn't the args be reversed??????????????
		m_channelIndexes.set(recordedChan, chani); // index, value
		m_fileIndexes.set(recordedChan, 0);
	}
	std::cout << "Enabled chans: " << JSON::toString(chans, true) << std::endl;
	std::cout << "Enabled chanNames: " << JSON::toString(chanNames, true) << std::endl;

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

	// get start timestamps for all enabled channels - should be the same for all?:
	int nsamples_offset = getTimestamp(0);
	for (int i = 0; i < nRecChans; i++)
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

	// collect .dat metadata in JSON data structure:
	DynamicObject::Ptr json = new DynamicObject();
	//json->setProperty("dat_fname", datFileName);
	json->setProperty("nchans", nRecChans); // number of enabled chans
	json->setProperty("sample_rate", sample_rate);
	json->setProperty("dtype", "int16");
	json->setProperty("uV_per_AD", uV_per_AD);
	/// TODO: parse the chanmap to extract probe_name:
	json->setProperty("probe_name", "");
	// add chans field only if some chans have been disabled:
	if (nRecChans != nHeadstageChans)
		json->setProperty("chans", chans);
	// normally don't have any analog input auxchans:
	//var auxchans;
	//if (auxchans)
		//json->setProperty("auxchans", auxchans);
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
		std::cerr << "Error creating JSON file:" << res.getErrorMessage() << std::endl;
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
	header += "## Processor start time is index of first sample in .dat file\n";
	header += "## Message log format: samplei <TAB> message\n";
	header += "samplei\tmessage\n"; // column names for loading into Pandas DataFrame
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
