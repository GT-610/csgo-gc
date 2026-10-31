#pragma once

namespace Panorama
{

// Relax the code.pbin signature check so a repacked Panorama frontend loads.
//
// Returns false and leaves the process untouched when the module is not loaded,
// the build is not recognised, or the located shape is ambiguous. Callers should
// treat false as "not applicable on this platform/build" rather than an error
// worth aborting the GC over.
//
// Only meaningful on the Windows/Linux/macOS client, which is where the Panorama
// frontend runs. Dedicated servers never load it, so this is a no-op there.
bool Apply(bool verbose = true);

} // namespace Panorama
