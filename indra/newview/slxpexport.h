#ifndef SLXPEXPORT_H_
#define SLXPEXPORT_H_

#include "llviewerprecompiledheaders.h"
#include "llviewerobject.h"

#include "aifilepicker.h"
#include "lljoint.h"


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
typedef std::vector<object_entry_t> object_entry_list_t;
typedef std::map<LLUUID, size_t> uuid_object_map_t;

struct ExportData
{
    std::string Title;
    uuid_object_map_t PendingObjects;
    object_entry_list_t Objects;

    ExportData(std::string title) :
        Title(title)
    {
    }
};

class SLXPExport {
public:
    static ExportData gExportData;
    static void HandleFilePicker(AIFilePicker* file_picker, ExportData export_data);
    static void processObjectProperties(LLMessageSystem* msg, void** user_data);
};

#endif // SLXPEXPORT_H_
