#include "slxpexport.h"

// library includes
#include "aifilepicker.h"
#include "llavatarnamecache.h"
#include "llnotificationsutil.h"

// newview includes
#include "lfsimfeaturehandler.h"
#include "llinventorymodel.h"
#include "llinventoryfunctions.h"
#include "llface.h"
#include "llversioninfo.h"
#include "llviewerinventory.h"
#include "llviewertexturelist.h"
#include "llvovolume.h"

// menu includes
#include "llevent.h"
#include "llmemberlistener.h"
#include "llselectmgr.h"

// Floater and UI
#include "llfloater.h"
#include "lluictrlfactory.h"
#include "llscrollcontainer.h"
#include "lltexturectrl.h"

#include "llmeshrepository.h"
#include "llvolume.h"
#include "llvolumemgr.h"
#include "llvoavatar.h"
#include "llvoavatarself.h"
#include "llcontrolavatar.h"

#include <format>
#include <fstream>

//Typedefs used in other files, using here for consistency.
typedef std::vector<LLAvatarJoint*> avatar_joint_list_t;
typedef LLMemberListener<LLView> view_listener_t;


inline void Copy(LLVector3 src, Vec3& dst)
{
    dst.x = src[VX];
    dst.y = src[VY];
    dst.z = src[VZ];
}

inline void Copy(LLQuaternion src, Vec4& dst)
{
    dst.x = src.mQ[VX];
    dst.y = src.mQ[VY];
    dst.z = src.mQ[VZ];
    dst.w = src.mQ[VW];
}

inline void Copy(LLVector2* src, size_t size, Vec2_list_t& dst)
{
    for (size_t i = 0; i < size; i++)
    {
        Vec2 v;
        v.x = src[i][VX];
        v.y = src[i][VY];
        dst.push_back(v);
    }
}

inline void Copy(LLVector4a* src, size_t size, Vec2_list_t& dst)
{
    for (size_t i = 0; i < size; i++)
    {
        Vec2 v;
        v.x = src[i][VX];
        v.y = src[i][VY];
        dst.push_back(v);
    }
}

inline void Copy(LLVector4a* src, size_t size, Vec3_list_t& dst)
{
    for (size_t i = 0; i < size; i++)
    {
        Vec3 v;
        v.x = src[i][VX];
        v.y = src[i][VY];
        v.z = src[i][VZ];
        dst.push_back(v);
    }
}

inline void Copy(LLVector4a* src, size_t size, Vec4_list_t& dst)
{
    for (size_t i = 0; i < size; i++)
    {
        Vec4 v;
        v.x = src[i][VX];
        v.y = src[i][VY];
        v.z = src[i][VZ];
        v.w = src[i][VW];
        dst.push_back(v);
    }
}

inline void Copy(U16* src, size_t size, Indices_list_t& dst)
{
    for (size_t i = 0; i < size; i++)
    {
        dst.push_back(src[i]);
    }
}

void AddJointsPreorder(std::vector<LLJoint*>& joints, LLJoint* joint)
{
    joints.push_back(joint);
    for (auto j : joint->mChildren)
        AddJointsPreorder(joints, j);
}

void HandleFilePicker(AIFilePicker* file_picker, ExportData export_data)
{
    if (file_picker->isCanceled())
    {
        LLNotificationsUtil::add("SLXPExportCancelled");
        return;
    }
    if (!file_picker->hasFilename())
    {
        LLSD args;
        args["REASON"] = "no file name provided.";
        LLNotificationsUtil::add("SLXPExportError", args);
        return;
    }
    if (export_data.Objects.size() == 0)
    {
        LLSD args;
        args["REASON"] = "no objects selected for export.";
        LLNotificationsUtil::add("SLXPExportError", args);
        return;
    }
    // Do export
    SLXP document(export_data.Title);
    int warnings = 0;
    for (auto& entry : export_data.Objects)
    {
        auto& name = entry.Name;
        auto& obj = entry.Value;
        try {

            if (entry.Value->isAvatar())
            {
                auto avatar = (LLVOAvatar*)obj;
                auto root_joint = avatar->getRootJoint();
                std::vector<LLJoint*> joints;
                AddJointsPreorder(joints, root_joint);
                for (auto& joint : joints)
                {
                    SLXPJoint slxp_joint(joint->getName(), joint->getJointNum());
                    if (joint->getParent())
                        slxp_joint.ParentId = joint->getParent()->getJointNum();
                    Copy(obj->getPosition(), slxp_joint.LocalPosition);
                    Copy(obj->getRotation(), slxp_joint.LocalRotation);
                    Copy(obj->getScale(), slxp_joint.LocalScale);
                    document.Collection.Joints.push_back(slxp_joint);
                }
                continue;
            }

            SLXPObject slxp_obj(name, obj->getLocalID());
            auto parent = (LLViewerObject*)obj->getParent();
            if (parent)
                slxp_obj.ParentId = parent->getLocalID();

            if (obj->isRiggedMesh())
            {
                // Cache the object's bind shape matrix to be applied to be applied to vertices later.
                auto obj_vov = (LLVOVolume*)obj;

                // Copy skin info with dereference
                const auto skin_info = obj_vov->getSkinInfo();

                const auto& bind_shape_mtx = skin_info->mBindShapeMatrix;
                slxp_obj.setBindShapeMatrix(bind_shape_mtx.mMatrix);
            }
            auto vol = obj->getVolume();
            auto& faces = vol->getVolumeFaces();
            for (auto& face : faces)
            {
                SLXPFace slxp_face;
                Copy(face.mPositions, face.mNumVertices, slxp_face.Positions);
                Copy(face.mNormals, face.mNumVertices, slxp_face.Normals);
                if (face.mTangents)
                    Copy(face.mTangents, face.mNumVertices, slxp_face.Tangents);
                Copy(face.mTexCoords, face.mNumVertices, slxp_face.TexCoords);
                if (face.mWeights)
                    Copy(face.mWeights, face.mNumVertices, slxp_face.Weights);
                Copy(face.mIndices, face.mNumIndices, slxp_face.Indices);
                slxp_obj.Faces.push_back(slxp_face);
            }
            Copy(obj->getPosition(), slxp_obj.LocalPosition);
            Copy(obj->getRotation(), slxp_obj.LocalRotation);
            Copy(obj->getScale(), slxp_obj.LocalScale);
            document.Collection.Objects.push_back(slxp_obj);
        }
        catch (std::exception e)
        {
            LL_WARNS() << "Exception thrown while encoding object named \"" << entry.Name << "\":" << LL_ENDL;
            LL_WARNS() << e.what() << LL_ENDL;
            warnings++;
        }
    }
    if (warnings > 0)
    {
        LLSD args;
        args["REASON"] = llformat("exported with %d warnings.", warnings);
        LLNotificationsUtil::add("SLXPExportWarning");
        return;
    }

    std::fstream os;
    os.open(file_picker->getFilename(), std::fstream::out);
    //document.serialize(os);
    os << document.toJSON() << std::endl;
    os.close();

    LLNotificationsUtil::add("SLXPExportSuccessful");
}


LLObjectSelectionHandle GetSelection()
{
    return LLSelectMgr::getInstance()->getSelection();
}

std::string GetObjectName(LLViewerObject* object)
{
    LLNameValue* title = object->getNVPair("Title");
    if (title)
        return title->getString();
    return "Object";
}

void GetSelectionObjects(LLObjectSelectionHandle selection, std::vector<object_entry_t>& objects)
{
    for (LLObjectSelection::iterator iter = selection->begin(); iter != selection->end(); ++iter)
    {
        auto node = *iter;
        if (!node->getObject()->getVolume())
            continue;
        objects.push_back(object_entry_t(node->mName, node->getObject()));
    }
}

void GetAvatarAttachments(LLVOAvatar* avatar, std::vector<object_entry_t>& objects)
{
    for (auto& iter : avatar->mAttachedObjectsVector)
    {
        LLViewerObject* object = iter.first;
        if (!object || !object->getVolume())
            continue;

        // Completely ignore/skip over HUD attachments
        // TODO: include HUD attachments and handle at import time, etc.
        if (object->isHUDAttachment())
            continue;

        objects.push_back(object_entry_t(GetObjectName(object), object));
        auto& children = object->getChildren();
        for (auto& child : children)
        {
            if (!child || !child->getVolume())
                continue;
            objects.push_back(object_entry_t(GetObjectName(child), child));
        }
    }
}

class SLXPSaveSelectedObjects final : public view_listener_t
{
    bool handleEvent(LLPointer<LLOldEvents::LLEvent> event, const LLSD& userdata)
    {
        auto selection = GetSelection();
        if (selection && selection->getFirstRootObject())
        {
            auto root = selection->getFirstRootNode();
            auto file_picker = AIFilePicker::create();
            ExportData export_data(root->mName);
            GetSelectionObjects(selection, export_data.Objects);
            file_picker->open(root->mName + ".slxp");
            file_picker->run(boost::bind(&HandleFilePicker, file_picker, export_data));
        }
        return true;
    }
};

class SLXPSaveSelectedAvatar final : public view_listener_t
{
    bool handleEvent(LLPointer<LLOldEvents::LLEvent> event, const LLSD& userdata)
    {
        auto selection = GetSelection();
        LLViewerObject* primary_object = selection->getPrimaryObject();
        if (primary_object && primary_object->isAvatar())
        {
            auto avatar = (LLVOAvatar*)primary_object;
            auto file_picker = AIFilePicker::create();
            ExportData export_data(avatar->getFullname());

            export_data.Objects.push_back(object_entry_t(avatar->getFullname(), avatar));
            GetAvatarAttachments(avatar, export_data.Objects);
            file_picker->open(avatar->getFullname() + ".slxp");
            file_picker->run(boost::bind(&HandleFilePicker, file_picker, export_data));
        }
        return true;
    }
};

void addMenu(view_listener_t* menu, const std::string& name);
void add_slxp_listeners() // Called in llviewermenu with other addMenu calls, function linked against
{
    addMenu(new SLXPSaveSelectedObjects(), "Object.SaveAsSLXP");
    addMenu(new SLXPSaveSelectedAvatar(), "Avatar.SaveAsSLXP");
}
