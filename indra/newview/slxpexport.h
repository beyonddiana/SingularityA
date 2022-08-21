#ifndef SLXPEXPORT_H_
#define SLXPEXPORT_H_

#include "llviewerprecompiledheaders.h"
#include "llviewerobject.h"

#include "aifilepicker.h"
#include "lljoint.h"
#include "slxp.h"


template <typename T>
struct NamedValue {
    std::string Name;
    T Value;
    NamedValue(std::string name, T value) :
        Name(name),
        Value(value)
    {}
};

typedef NamedValue<LLViewerObject*> object_entry_t;

struct ExportData
{
    std::string Title;
    std::vector<object_entry_t> Objects;

    ExportData(std::string title) :
        Title(title)
    {
    }
};

void HandleFilePicker(AIFilePicker* file_picker, ExportData export_data);

#endif // SLXPEXPORT_H_

