#pragma once

// Widens how far the viewmodel_offset_x/y/z console variables can go.
//
// The game keeps those three between -2 and 2 (or similar), which is why the
// weapon can't be moved far. This finds them in client.dll and multiplies their
// allowed range, so "viewmodel_offset_x 15" sticks. The layout is checked
// before anything is changed, and nothing happens if it doesn't match.
//
// Windows only for the search; ScaleViewmodelRange works on any ConVar memory.

// multiplies the allowed range of one ConVar object (min and max). false, with
// nothing changed, if the memory doesn't look like a ConVar with a range
bool ScaleViewmodelRange(void *convar, float multiplier);

// finds the viewmodel offset variables in client.dll and widens them. Safe to
// call repeatedly: it does its job once client.dll is loaded
void TryApplyViewmodelRange();
