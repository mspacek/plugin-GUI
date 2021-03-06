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
    // found the channel source
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
    var chanis;
    var chanNames;
    for (int chani = 0; chani < nRecChans; chani++)
    {
        int recordedChan = pInfo0.recordedChannels[chani];
        /// TODO: what's the difference between recordedChan and realChan?
        int realChan = getRealChannel(recordedChan); // does some kind of dereferencing?
        const DataChannel* datachan = getDataChannel(realChan);
        //chanis.append(recordedChan);
        chanis.append(realChan);
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
            std::cerr << "ERROR: sample rate of chan " << realChan << " == "
                      << datachan->getSampleRate() << " != " << sample_rate << std::endl;
        if (datachan->getBitVolts() != uV_per_AD)
            std::cerr << "ERROR: uV_per_AD of chan " << realChan << " == "
                      << datachan->getBitVolts() << " != " << uV_per_AD << std::endl;

        // fill in m_channelIndexes and m_fileIndexes for use in writeData, though
        // given the simplified setup assumed, these might not be necessary any more:
        /// TODO: is this right? shouldn't the args be reversed??????????????
        m_channelIndexes.set(recordedChan, chani); // index, value
        m_fileIndexes.set(recordedChan, 0);
    }
    std::cout << "Recording chanis: " << JSON::toString(chanis, true) << std::endl;
    std::cout << "Recording chanNames: " << JSON::toString(chanNames, true) << std::endl;

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

    // parse the chanmap to extract probe_name and adapter_name, separated by __:
    String sep = "__";
    var chanmapnames = CoreServices::getChannelMapNames();
    if (chanmapnames.size() != 1)
    {
        std::cerr << "ERROR: Need exactly 1 channel map, found: "
                  << JSON::toString(chanmapnames, true) << std::endl;
        JUCEApplication::quit();
    }
    String chanmapname = chanmapnames[0];
    std::cout << "Extracting probe and adapter names from channel map name '" << chanmapname
              << "'" << std::endl;
    String probe_name = chanmapname.upToFirstOccurrenceOf(sep, false, false);
    String adapter_name = chanmapname.fromFirstOccurrenceOf(sep, false, false);
    std::cout << "Extracted probe_name: " << probe_name << std::endl;
    std::cout << "Extracted adapter_name: " << adapter_name << std::endl;

    // collect chans array:
    std::cout << "Assuming '" << probe_name << "' chans are 1-based" << std::endl;
    int chanbase = 1;
    var chans;
    for (int i = 0; i < nRecChans; i++)
        chans.append((int)chanis[i] + chanbase); // convert to chanbase-based chans
    std::cout << "Saving headstage chans: " << JSON::toString(chans, true) << std::endl;

    // Handle auxchans here
    //var auxchans;

    // collect .dat metadata in JSON data structure:
    DynamicObject::Ptr json = new DynamicObject();
    //json->setProperty("dat_fname", datFileName);
    json->setProperty("nchans", nRecChans); // number of enabled headstage (?) chans
    json->setProperty("sample_rate", sample_rate);
    json->setProperty("dtype", "int16");
    json->setProperty("uV_per_AD", uV_per_AD);
    json->setProperty("probe_name", probe_name);
    if (adapter_name != "") // add adapter_name field only if adapter is defined for this probe
        json->setProperty("adapter_name", adapter_name);
    // add chans field only if some chans have been disabled:
    if (nRecChans != nHeadstageChans)
        json->setProperty("chans", chans);
    // normally don't have any analog input auxchans:
    //if (auxchans)
        //json->setProperty("auxchans", auxchans);
    json->setProperty("nsamples_offset", nsamples_offset);
    json->setProperty("datetime", datetime);
    json->setProperty("author", "Open-Ephys, BusseLabBinaryWriter plugin");
    String version = CoreServices::getGUIVersion() + ", " + BusseLabBinaryWriterPluginVersion;
    json->setProperty("version", version);
    json->setProperty("notes", "");

    std::cout << "METADATA:" << std::endl;
    String jsonstr = JSON::toString(var(json), false); // JUCE 5.3.2 has maximumDecimalPlaces
    std::cout << jsonstr << std::endl;

    // write .dat metadata to .dat.json file:
    String jsonFileName = basepath;
    jsonFileName += getRecordingNumberString(recordingNumber) + ".dat.json";
    File jsonf = File(jsonFileName);
    Result res = jsonf.create();
    if (res.failed())
        std::cerr << "Error creating JSON file:" << res.getErrorMessage() << std::endl;
    ScopedPointer<FileOutputStream> jsonFile = jsonf.createOutputStream();
    std::cout << "WRITING FILE: " << jsonFileName << std::endl;
    // this writeText() is from JUCE 5.3.2, see commit 06be1c2:
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
                    // this writeText() is from JUCE 5.3.2, see commit 06be1c2:
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
    // clear all stored objects, including open file handles?
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
        // this writeText() is from JUCE 5.3.2, see commit 06be1c2:
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
        // if old and new words differ at the experiment bit, flush to disk so that online
        // analysis can be performed on the dinFile:
        int64 expBitChanged = (word ^ m_lastTTLWord) & m_experimentBit;
        //std::cout << "expBitChanged: " << expBitChanged << std::endl;
        if (expBitChanged)
        {
            std::cout << "Experiment bit change detected, flushing .din to disk" << std::endl;
            rec->dataFile->updateHeader();
        }
        m_lastTTLWord = word; // update
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
    // this writeText() is from JUCE 5.3.2, see commit 06be1c2:
    m_msgFile->writeText("## " + text + "\n", false, false, nullptr);
    m_msgFile->flush();
}

void BinaryRecording::writeSpike(int electrodeIndex, const SpikeEvent* spike)
{
    int64 ts = spike->getTimestamp();
    /*
      electrodeIndex is really just the plot index, which isn't relevant
      (each electrode has exactly 1 plot, because only single electrodes are allowed now)
      What we care about is the actual data channel that plot is plotting, so that's
      what we'll write to file.
    */
    const SpikeChannel* spikeChan = spike->getChannelInfo();
    String chanName = spikeChan->getName(); // should represent probe channel, not ADC channel
    // "CH": ADC chan, for channel maps without "labels" field
    // "PR": probe chan, for channel maps with "labels" field
    // strip "PR" from start of chanName, use remaining string as numeric ID, convert to int64:
    if (!chanName.startsWith("PR"))
    {
        std::cerr << "ERROR: data must be piped through a channel map with a 'labels'"
                     "field specifying probe channel labels, resulting in all chanNames "
                     "starting with 'PR'" << std::endl;
        std::cerr << "Got chanName: "  << chanName << std::endl;
        JUCEApplication::quit();
    }
    String chanIDstr = chanName.trimCharactersAtStart("PR");
    int64 chanID = chanIDstr.getLargeIntValue();
    int64 sortedID = (int64)(uint16)spike->getSortedID();
    EventRecording* rec = m_spikeFile;
    rec->dataFile->writeData(&ts, sizeof(int64)); // timestamp
    rec->dataFile->writeData(&chanID, sizeof(int64)); // spike channel
    rec->dataFile->writeData(&sortedID, sizeof(int64)); // cluster ID
    increaseEventCounts(rec);
    //std::cout << "ts " << ts << std::endl;
    //std::cout << "chanID " << chanID << std::endl;
    //std::cout << "sortedID " << sortedID << std::endl;
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
