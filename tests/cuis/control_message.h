/* Control-channel fixture helper: tolerate normal asynchronous redraws. */
#ifndef CUIS_CONTROL_MESSAGE_H
#define CUIS_CONTROL_MESSAGE_H
static short control_message(short message[8])
{
    short result;
    do { result = evnt_mesag(message); } while (result && message[0] == WM_REDRAW);
    return result;
}
#endif
