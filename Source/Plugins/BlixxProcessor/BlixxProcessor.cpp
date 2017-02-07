
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

    if (events.getNumEvents() > 0)
    {

        // int m = events.getNumEvents();
        //std::cout << m << " events received by node " << getNodeId() << std::endl;

        MidiBuffer::Iterator i(events);
        MidiMessage message(0xf4); // allocate memory for a MidiMessage for iteration

        String blixxstr = "BLIXXFRAME";
        CharPointer_UTF8 blixxcharp = blixxstr.toUTF8(); // maybe this should have a \n at the end for parsing?

        int vsyncChannel = 1;

        int samplePosition = 0;
        i.setNextSamplePosition(samplePosition);

        while (i.getNextEvent(message, samplePosition))
        {
            const uint8* dataptr = message.getRawData();
            int eventType = *(dataptr+0);
            int sampleNum = *(dataptr+1);
            int eventId = *(dataptr+2);
            int eventChannel = *(dataptr+3);

            if (eventType == TTL && eventChannel == vsyncChannel && eventId == RISING)
                //&& all the other relevant pins are already high
            {
                addEvent(events,    // MidiBuffer
                         MESSAGE,   // eventType
                         sampleNum, // sampleNum
                         nodeId,         // eventID
                         0,              // eventChannel
                         blixxcharp.sizeInBytes(),      // numBytes
                         (uint8*)blixxcharp.getAddress()); // data
            }

        }

    }

    return -1;

}
