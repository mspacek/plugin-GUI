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
* get channel map working
* parse the chanmap to extract probe_name
* need to deal with 0 vs 1-based channel IDs - which to use depends on probe type
* test that enabled chans are written correctly to .json when some chans are disabled
* handle auxchans, if any, in .json
* give plugin a version number, add to .json?
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
	int refts = getTimestamp(0);
	for (int i = 0; i < nChans; i++)
	{
		if (i == 0)
			std::cout << "Start timestamp: " << refts << std::endl;
		jassert(getTimestamp(i) == refts);
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
	json->setProperty("nsamples_offset", m_startTS[0]);
	json->setProperty("datetime", datetime);
	json->setProperty("author", "Open-Ephys, BusseLabBinaryWriter plugin");
	json->setProperty("version", CoreServices::getGUIVersion());
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
				m_dinFile = rec.release(); // store file handle
				break;
			}
		}
	}

	// open spike files:
	int nSpikes = getNumRecordedSpikes();
	Array<const SpikeChannel*> indexedSpikes;
	Array<uint16> indexedChannels;
	m_spikeFileIndexes.insertMultiple(0, 0, nSpikes);
	m_spikeChannelIndexes.insertMultiple(0, 0, nSpikes);
	String spikePath(basepath + "spikes" + File::separatorString);
	Array<var> jsonSpikeFiles;
	Array<var> jsonSpikeChannels;
	std::map<uint32, int> groupMap;
	for (int sp = 0; sp < nSpikes; sp++)
	{
		const SpikeChannel* ch = getSpikeChannel(sp);
		DynamicObject::Ptr jsonChannel = new DynamicObject();
		unsigned int numSpikeChannels = ch->getNumChannels();
		jsonChannel->setProperty("channel_name", ch->getName());
		jsonChannel->setProperty("description", ch->getDescription());
		jsonChannel->setProperty("identifier", ch->getIdentifier());
		Array<var> jsonChannelInfo;
		for (int i = 0; i < numSpikeChannels; i++)
		{
			SourceChannelInfo sourceInfo = ch->getSourceChannelInfo()[i];
			DynamicObject::Ptr jsonSpikeChInfo = new DynamicObject();
			jsonSpikeChInfo->setProperty("source_processor_id", sourceInfo.processorID);
			jsonSpikeChInfo->setProperty("source_processor_sub_idx", sourceInfo.subProcessorID);
			jsonSpikeChInfo->setProperty("source_processor_channel", sourceInfo.channelIDX);
			jsonChannelInfo.add(var(jsonSpikeChInfo));
		}
		jsonChannel->setProperty("source_channel_info", jsonChannelInfo);
		createChannelMetaData(ch, jsonChannel);

		int nIndexed = indexedSpikes.size();
		bool found = false;
		for (int i = 0; i < nIndexed; i++)
		{
			const SpikeChannel* ich = indexedSpikes[i];
			// identical channels (same data and metadata) from the same processor
			// will be saved to the same file:
			if (ch->getSourceNodeID() == ich->getSourceNodeID() &&
			    ch->getSubProcessorIdx() == ich->getSubProcessorIdx() &&
			    *ch == *ich)
			{
				found = true;
				m_spikeFileIndexes.set(sp, i);
				unsigned int numChans = indexedChannels[i];
				indexedChannels.set(i, numChans);
				m_spikeChannelIndexes.set(sp, numChans + 1);
				jsonSpikeChannels.getReference(i).append(var(jsonChannel));
				break;
			}
		}
		
		if (!found) // write spike data to file
		{
			int fileIndex = m_spikeFiles.size();
			m_spikeFileIndexes.set(sp, fileIndex);
			indexedSpikes.add(ch);
			m_spikeChannelIndexes.set(sp, 0);
			indexedChannels.add(1);
			ScopedPointer<EventRecording> rec = new EventRecording();
			
			uint32 procID = GenericProcessor::getProcessorFullId(ch->getSourceNodeID(), ch->getSubProcessorIdx());
			int groupIndex = ++groupMap[procID];

			String spikeName = getProcessorString(ch) + "spike_group_" + String(groupIndex) + File::separatorString;

			rec->dataFile = new NpyFile(spikePath + spikeName + "spike_waveforms.npy", NpyType(BaseType::INT16, ch->getTotalSamples()), ch->getNumChannels());
			rec->tsFile = new NpyFile(spikePath + spikeName + "spike_times.npy", NpyType(BaseType::INT64, 1));
			rec->chanFile = new NpyFile(spikePath + spikeName + "spike_electrode_indices.npy", NpyType(BaseType::UINT16, 1));
			rec->extraFile = new NpyFile(spikePath + spikeName + "spike_clusters.npy", NpyType(BaseType::UINT16, 1));
			Array<NpyType> tsTypes;
			
			Array<var> jsonChanArray;
			jsonChanArray.add(var(jsonChannel));
			jsonSpikeChannels.add(var(jsonChanArray));
			DynamicObject::Ptr jsonFile = new DynamicObject();
			
			jsonFile->setProperty("folder_name", spikeName.replace(File::separatorString,"/"));
			jsonFile->setProperty("sample_rate", ch->getSampleRate());
			jsonFile->setProperty("source_processor", ch->getSourceName());
			jsonFile->setProperty("num_channels", (int)numSpikeChannels);
			jsonFile->setProperty("pre_peak_samples", (int)ch->getPrePeakSamples());
			jsonFile->setProperty("post_peak_samples", (int)ch->getPostPeakSamples());
			
			rec->metaDataFile = createEventMetadataFile(ch, spikePath + spikeName + ".metadata.npy", jsonFile);
			m_spikeFiles.add(rec.release());
			jsonSpikeFiles.add(var(jsonFile));
		}
	}
	int nSpikeFiles = jsonSpikeFiles.size();
	for (int i = 0; i < nSpikeFiles; i++)
	{
		int size = jsonSpikeChannels.getReference(i).size();
		DynamicObject::Ptr jsonFile = jsonSpikeFiles.getReference(i).getDynamicObject();
		jsonFile->setProperty("num_channels", size);
		jsonFile->setProperty("channels", jsonSpikeChannels.getReference(i));
	}

	m_recordingNum = recordingNumber;

}

NpyFile* BinaryRecording::createEventMetadataFile(const MetaDataEventObject* channel,
												  String filename, DynamicObject* jsonFile)
{
	int nMetaData = channel->getEventMetaDataCount();
	if (nMetaData < 1) return nullptr;

	Array<NpyType> types;
	Array<var> jsonMetaData;
	for (int i = 0; i < nMetaData; i++)
	{
		const MetaDataDescriptor* md = channel->getEventMetaDataDescriptor(i);
		types.add(NpyType(md->getName(), md->getType(), md->getLength()));
		DynamicObject::Ptr jsonValues = new DynamicObject();
		jsonValues->setProperty("name", md->getName());
		jsonValues->setProperty("description", md->getDescription());
		jsonValues->setProperty("identifier", md->getIdentifier());
		jsonValues->setProperty("type", jsonTypeValue(md->getType()));
		jsonValues->setProperty("length", (int)md->getLength());
		jsonMetaData.add(var(jsonValues));
	}
	if (jsonFile)
		jsonFile->setProperty("event_metadata", jsonMetaData);
	return new NpyFile(filename, types);
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

void BinaryRecording::createChannelMetaData(const MetaDataInfoObject* channel,
											DynamicObject* jsonFile)
{
	int nMetaData = channel->getMetaDataCount();
	if (nMetaData < 1) return;

	Array<var> jsonMetaData;
	for (int i = 0; i < nMetaData; i++)
	{
		const MetaDataDescriptor* md = channel->getMetaDataDescriptor(i);
		const MetaDataValue* mv = channel->getMetaDataValue(i);
		DynamicObject::Ptr jsonValues = new DynamicObject();
		MetaDataDescriptor::MetaDataTypes type = md->getType();
		unsigned int length = md->getLength();
		jsonValues->setProperty("name", md->getName());
		jsonValues->setProperty("description", md->getDescription());
		jsonValues->setProperty("identifier", md->getIdentifier());
		jsonValues->setProperty("type", jsonTypeValue(type));
		jsonValues->setProperty("length", (int)length);
		var val;
		if (type == MetaDataDescriptor::CHAR)
		{
			String tmp;
			mv->getValue(tmp);
			val = tmp;
		}
		else
		{
			const void* buf = mv->getRawValuePointer();
			switch (type)
			{
			case MetaDataDescriptor::INT8:
				dataToVar<int, int8>(val, buf, length);
				break;
			case MetaDataDescriptor::UINT8:
				dataToVar<int, uint8>(val, buf, length);
				break;
			case MetaDataDescriptor::INT16:
				dataToVar<int, int16>(val, buf, length);
				break;
			case MetaDataDescriptor::UINT16:
				dataToVar<int, uint16>(val, buf, length);
				break;
			case MetaDataDescriptor::INT32:
				dataToVar<int, int32>(val, buf, length);
				break;
			case MetaDataDescriptor::UINT32:
				// a full uint32 doesn't fit in a regular int, so increase size:
				dataToVar<int64, uint8>(val, buf, length);
				break;
			case MetaDataDescriptor::INT64:
				dataToVar<int64, int64>(val, buf, length);
				break;
			case MetaDataDescriptor::UINT64:
				// this might overrun and end negative if the uint64 is really big,
				// but there is no way to store a full uint64 in a var:
				dataToVar<int64, uint64>(val, buf, length);
				break;
			case MetaDataDescriptor::FLOAT:
				dataToVar<float, float>(val, buf, length);
				break;
			case MetaDataDescriptor::DOUBLE:
				dataToVar<double, double>(val, buf, length);
				break;
			default:
				val = "invalid";
			}
		}
		jsonValues->setProperty("value", val);
		jsonMetaData.add(var(jsonValues));
	}
	jsonFile->setProperty("channel_metadata", jsonMetaData);
}

void BinaryRecording::closeFiles()
{
	resetChannels();
}

void BinaryRecording::resetChannels()
{
	// dereferencing all file handles closes all files?
	m_DataFiles.clear();
	m_channelIndexes.clear();
	m_fileIndexes.clear();
	m_eventFiles.clear();
	m_spikeFiles.clear();
	m_spikeChannelIndexes.clear();
	m_spikeFileIndexes.clear();
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

void BinaryRecording::writeEventMetaData(const MetaDataEvent* event, NpyFile* file)
{
	std::cout << "in writeEventMetaData()" << std::endl;
	if (!file || !event) return;
	int nMetaData = event->getMetadataValueCount();
	for (int i = 0; i < nMetaData; i++)
	{
		const MetaDataValue* val = event->getMetaDataValue(i);
		file->writeData(val->getRawValuePointer(), val->getDataSize());
	}
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
					+ ", BusseLabBinaryWriter plugin\n";
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
	const SpikeChannel* channel = getSpikeChannel(electrodeIndex);
	EventRecording* rec = m_spikeFiles[m_spikeFileIndexes[electrodeIndex]];
	uint16 spikeChannel = m_spikeChannelIndexes[electrodeIndex];

	int totalSamples = channel->getTotalSamples() * channel->getNumChannels();


	if (totalSamples > m_bufferSize)
	// shouldn't happen, and if it does it'll be slow, but better this than crashing
	{
		std::cerr << "(spike) Write buffer overrun, resizing to" << totalSamples << std::endl;
		m_bufferSize = totalSamples;
		m_scaledBuffer.malloc(totalSamples);
		m_intBuffer.malloc(totalSamples);
	}
	double multFactor = 1 / (float(0x7fff) * channel->getChannelBitVolts(0));
	FloatVectorOperations::copyWithMultiply(m_scaledBuffer.getData(), spike->getDataPointer(),
											multFactor, totalSamples);
	AudioDataConverters::convertFloatToInt16LE(m_scaledBuffer.getData(), m_intBuffer.getData(),
											   totalSamples);
	rec->dataFile->writeData(m_intBuffer.getData(), totalSamples*sizeof(int16));
	
	int64 ts = spike->getTimestamp();
	rec->tsFile->writeData(&ts, sizeof(int64));

	rec->chanFile->writeData(&spikeChannel, sizeof(uint16));

	uint16 sortedID = spike->getSortedID();
	rec->extraFile->writeData(&sortedID, sizeof(uint16));
	writeEventMetaData(spike, rec->metaDataFile);

	increaseEventCounts(rec);
}

void BinaryRecording::increaseEventCounts(EventRecording* rec)
{
	rec->dataFile->increaseRecordCount();
	if (rec->tsFile) rec->tsFile->increaseRecordCount();
	if (rec->extraFile) rec->extraFile->increaseRecordCount();
	if (rec->chanFile) rec->chanFile->increaseRecordCount();
	if (rec->metaDataFile) rec->metaDataFile->increaseRecordCount();
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

String BinaryRecording::jsonTypeValue(BaseType type)
{
	switch (type)
	{
	case BaseType::CHAR:
		return "string";
	case BaseType::INT8:
		return "int8";
	case BaseType::UINT8:
		return "uint8";
	case BaseType::INT16:
		return "int16";
	case BaseType::UINT16:
		return "uint16";
	case BaseType::INT32:
		return "int32";
	case BaseType::UINT32:
		return "uint32";
	case BaseType::INT64:
		return "int64";
	case BaseType::UINT64:
		return "uint64";
	case BaseType::FLOAT:
		return "float";
	case BaseType::DOUBLE:
		return "double";
	default:
		return String::empty;
	}
}
