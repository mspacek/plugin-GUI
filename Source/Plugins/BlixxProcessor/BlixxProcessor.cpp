
/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2014 Open Ephys

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


#include <stdio.h>
#include "BlixxProcessor.h"

//If the processor uses a custom editor, it needs its header to instantiate it
#include "BlixxEditor.h"

BlixxProcessor::BlixxProcessor()
    : GenericProcessor("Blixx Processor") //, threshold(200.0), state(true)
{
    //Without a custom editor, generic parameter controls can be added
    //parameters.add(Parameter("thresh", 0.0, 500.0, 200.0, 0));

    /* It's critical to set processor type here as Filter, instead of the default Utility,
     * to ensure its messages are saved to disk */
    setProcessorType (PROCESSOR_TYPE_FILTER);

    blixxEvents = MidiBuffer();
}

BlixxProcessor::~BlixxProcessor()
{

}

/**
    If the processor uses a custom editor, this method must be present.
*/

AudioProcessorEditor* BlixxProcessor::createEditor()
{
    editor = new BlixxEditor(this, true);

    std::cout << "Creating Blixx editor." << std::endl;

    return editor;
}

void BlixxProcessor::setParameter(int parameterIndex, float newValue)
{

    //Parameter& p =  parameters.getReference(parameterIndex);
    //p.setValue(newValue, 0);

    //threshold = newValue;

    //std::cout << float(p[0]) << std::endl;
    editor->updateParameterButtons(parameterIndex);
}

void BlixxProcessor::process(AudioSampleBuffer& buffer,
                             MidiBuffer& events)
{
    /**
    Generic structure for processing buffer data
    */

    checkForEvents(events);

}

int BlixxProcessor::checkForEvents(MidiBuffer& events)


{
    int numEvents = events.getNumEvents();
    if (numEvents > 0)
    {
        std::cout << "*** " << numEvents << " events received by Blixx node " << getNodeId() << std::endl;
        //std::cout << "Blixx events: " << &events << std::endl;
        MidiBuffer::Iterator i(events);
        MidiMessage message (0xf4); // allocate memory for a MidiMessage for iteration

        int vsyncChannel = 0; // digital input line, 0-based, 0--15 are possible I think

        int nblixx = 0; // num BLIXX frame draws detected in this event buffer

        int samplePosition = 0;
        i.setNextSamplePosition(samplePosition); // is this necessary?

        while (i.getNextEvent(message, samplePosition))
        {
            const uint8* dataptr = message.getRawData();
            int eventType = *(dataptr+0);
            int sourceNodeId = *(dataptr+1);
            //uint8 sourceNodeId = message.getNoteNumber();
            int eventId = *(dataptr+2);
            int eventChannel = *(dataptr+3);
            int save = *(dataptr+4);

            printf("eventType, sourceNodeId, eventId, eventChannel, save, samplePosition: %d, %d, %d, %d, %d, %d\n",
                    eventType, sourceNodeId, eventId, eventChannel, save, samplePosition);

            if (eventType == TTL && eventChannel == vsyncChannel && eventId == RISING)
                //&& all the other relevant pins are already high

            {
                String blixxstr = "BLIXXFRAME";
                CharPointer_UTF8 blixxstrdata = blixxstr.toUTF8();
                //printf("size, length: %zu, %zu\n", blixxstrdata.sizeInBytes(), blixxstrdata.length());
                //std::cout << "* adding event" << std::endl;
                addEvent(blixxEvents, // MidiBuffer to add event to
                         MESSAGE, // eventType
                         samplePosition, // sampleNum
                         eventId, // eventID
                         0, // eventChannel
                         // use length() instead of sizeInBytes(), to avoid saving NULL terminator
                         // and make the string readable in a text editor:
                         blixxstrdata.length(), // numBytes
                         (uint8*)blixxstrdata.getAddress()); // data

                nblixx++;
                std::cout << "*** detected BLIXX event" << std::endl;
            }

        }

        if (nblixx > 0)
        {
            // add all blixxEvents to system-wide events buffer:
            events.addEvents(blixxEvents, 0, -1, 0);
            std::cout << "*** added " << nblixx << " BLIXX events to system event buffer" << std::endl;
        }
    }

    return -1;

}
