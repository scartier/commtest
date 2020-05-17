// TILE COMMUNICATIONS
// by Scott Cartier
//
// Tiles use the value on their faces to communicate to their neighbors.
// They can send six bits of data (0-63).
//
// The protocol here uses one of the six bits as a TOGGLE bit that changes
// with every new piece of data. Additionally, one bit is an ACK bit that
// tells the neighbor when the current value has been received.
//
// Because one bit toggles with every new value sent, the receiver can
// always tell when there is new data to read.
//
// Bit 5: ACK BIT - Equals the last received TOGGLE BIT from the neighbor.
// Bit 4: TOGGLE BIT - Toggles between 1 and 0 (command and data).
// Bit 3-0: COMMAND or DATA - The payload value.
//
// When the neighbor's ACK BIT equals our TOGGLE BIT we know the neighbor 
// processed our current value and is ready for the next one.

#define TOGGLE_COMMAND 1
#define TOGGLE_DATA 0
struct FaceValue
{
  byte value : 4;
  byte toggle : 1;
  byte ack : 1;
};

struct NeighborState
{
  FaceValue faceValueIn;
  FaceValue faceValueOut;
  byte lastCommandIn;

  byte ourState;
  byte neighborState;
  // YOUR STUFF GOES HERE
};
NeighborState neighborStates[FACE_COUNT];

enum CommandType
{
  CommandType_None,         // no data
  CommandType_UpdateState,  // data=value
  // YOUR STUFF GOES HERE
};

struct CommandAndData
{
  CommandType command : 4;
  byte data : 4;
};

#define COMM_QUEUE_SIZE 4
CommandAndData commQueues[FACE_COUNT][COMM_QUEUE_SIZE];

#define COMM_INDEX_ERROR_OVERRUN 0xFF
#define COMM_INDEX_OUT_OF_SYNC   0xFE
byte commInsertionIndexes[FACE_COUNT];

#define ErrorOnFace(f) (commInsertionIndexes[f] > COMM_QUEUE_SIZE)

// Timer used to toggle between green & blue
Timer sendNewStateTimer;

void setup()
{
  FOREACH_FACE(f)
  {
    resetCommOnFace(f);
  }
}

void loop()
{
  updateCommOnFaces();

  if (sendNewStateTimer.isExpired())
  {
    FOREACH_FACE(f)
    {
      byte nextVal = neighborStates[f].neighborState == 2 ? 3 : 2;
      neighborStates[f].neighborState = nextVal;
      enqueueCommOnFace(f, CommandType_UpdateState, nextVal);
    }
    sendNewStateTimer.set(500);
  }

  render();
}

void resetCommOnFace(byte f)
{
  // Clear the queue
  commInsertionIndexes[f] = 0;

  // Put the current output into its reset state.
  // In this case, all zeroes works for us.
  // Assuming the neighbor is also reset, it means our ACK == their TOGGLE.
  // This allows the next pair to be sent immediately.
  // Also, since the toggle bit is set to TOGGLE_DATA, it will toggle into TOGGLE_COMMAND,
  // which is what we need to start sending a new pair.
  neighborStates[f].faceValueOut.value = 0;
  neighborStates[f].faceValueOut.toggle = TOGGLE_DATA;
  neighborStates[f].faceValueOut.ack = TOGGLE_DATA;
  sendValueOnFace(f, neighborStates[f].faceValueOut);
}

void sendValueOnFace(byte f, FaceValue faceValue)
{
  byte outVal = *((byte*)&faceValue);
  setValueSentOnFace(outVal, f);
}

// Called by the main program when this tile needs to tell something to
// a neighbor tile.
void enqueueCommOnFace(byte f, CommandType commandType, byte data)
{
  if (commInsertionIndexes[f] >= COMM_QUEUE_SIZE)
  {
    // Buffer overrun - might need to increase queue size to accommodate
    commInsertionIndexes[f] = COMM_INDEX_ERROR_OVERRUN;
    return;
  }

  byte index = commInsertionIndexes[f];
  commQueues[f][index].command = commandType;
  commQueues[f][index].data = data;
  commInsertionIndexes[f]++;
}

// Called every iteration of loop(), preferably before any main processing
// so that we can act on any new data being sent.
void updateCommOnFaces()
{
  FOREACH_FACE(f)
  {
    // Is the neighbor still there?
    if (isValueReceivedOnFaceExpired(f))
    {
      // Lost the neighbor - go back into reset on this face
      resetCommOnFace(f);
      continue;
    }

    // If there is any kind of error on the face then do nothing
    // The error can be reset by removing the neighbor
    if (ErrorOnFace(f))
    {
      continue;
    }

    NeighborState *neighborState = &neighborStates[f];

    // Read the neighbor's face value it is sending to us
    byte val = getLastValueReceivedOnFace(f);
    neighborState->faceValueIn = *((FaceValue*)&val);
    
    //
    // RECEIVE
    //

    // Did the neighbor send a new comm?
    // Recognize this when their TOGGLE bit changed from the last value we got.
    if (neighborState->faceValueOut.ack != neighborState->faceValueIn.toggle)
    {
      // Got a new comm - process it
      byte value = neighborState->faceValueIn.value;
      if (neighborState->faceValueIn.toggle == TOGGLE_COMMAND)
      {
        // This is the first part of a comm (COMMAND)
        // Save the command value until we get the data
        neighborState->lastCommandIn = value;
      }
      else
      {
        // This is the second part of a comm (DATA)
        // Use the saved command value to determine what to do with the data
        switch (neighborState->lastCommandIn)
        {
          // YOUR STUFF GOES HERE
          // For now, just save the value that was sent
          case CommandType_UpdateState:
            neighborState->ourState = value;
            break;
        }
      }

      // Acknowledge that we processed this value so the neighbor can send the next one
      neighborState->faceValueOut.ack = neighborState->faceValueIn.toggle;
    }
    
    //
    // SEND
    //
    
    // Did the neighbor acknowledge our last comm?
    // Recognize this when their ACK bit equals our current TOGGLE bit.
    if (neighborState->faceValueIn.ack == neighborState->faceValueOut.toggle)
    {
      // If we just sent the DATA half of the previous comm, check if there 
      // are any more commands to send.
      if (neighborState->faceValueOut.toggle == TOGGLE_DATA)
      {
        if (commInsertionIndexes[f] == 0)
        {
          // Nope, no more comms to send - bail and wait
          continue;
        }
      }

      // Send the next value, either COMMAND or DATA depending on the toggle bit

      // Toggle between command and data
      neighborState->faceValueOut.toggle = ~neighborState->faceValueOut.toggle;
      
      // Grab the first element in the queue - we'll need it either way
      CommandAndData commandAndData = commQueues[f][0];

      // Send either the command or data depending on the toggle bit
      if (neighborState->faceValueOut.toggle == TOGGLE_COMMAND)
      {
        neighborState->faceValueOut.value = commandAndData.command;
      }
      else
      {
        neighborState->faceValueOut.value = commandAndData.data;
  
        // No longer need this comm - shift everything towards the front of the queue
        for (byte commIndex = 1; commIndex < COMM_QUEUE_SIZE; commIndex++)
        {
          commQueues[f][commIndex-1] = commQueues[f][commIndex];
        }

        // Adjust the insertion index since we just shifted the queue
        if (commInsertionIndexes[f] == 0)
        {
          // Shouldn't get here - if so something is funky
          commInsertionIndexes[f] = COMM_INDEX_OUT_OF_SYNC;
          continue;
        }
        else
        {
          commInsertionIndexes[f]--;
        }
      }
    }
  }

  FOREACH_FACE(f)
  {
    // Update the value sent in case anything changed
    sendValueOnFace(f, neighborStates[f].faceValueOut);
  }
}

// YOUR STUFF GOES HERE
// Choose what colors to show on each face
void render()
{
  setColor(OFF);
  
  FOREACH_FACE(f)
  {
    NeighborState *neighborState = &neighborStates[f];

    if (ErrorOnFace(f))
    {
      if (commInsertionIndexes[f] == COMM_INDEX_ERROR_OVERRUN)
      {
        setColorOnFace(MAGENTA, f);
      }
      else if (commInsertionIndexes[f] == COMM_INDEX_OUT_OF_SYNC)
      {
        setColorOnFace(ORANGE, f);
      }
      else
      {
        setColorOnFace(RED, f);
      }
    }
    else if (!isValueReceivedOnFaceExpired(f))
    {
      if (neighborState->ourState == 2)
      {
        setColorOnFace(GREEN, f);
      }
      else if (neighborState->ourState == 3)
      {
        setColorOnFace(BLUE, f);
      }
    }
  }
}
