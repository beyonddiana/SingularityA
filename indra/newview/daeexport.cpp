/**
* @file daeexport.cpp
* @brief A system which allows saving in-world objects to Collada .DAE files for offline texturizing/shading.
* @authors Latif Khalifa
*
* $LicenseInfo:firstyear=2013&license=LGPLV2.1$
* Copyright (C) 2013 Latif Khalifa
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General
* Public License along with this library; if not, write to the
* Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
* Boston, MA 02110-1301 USA
*/
#include "llviewerprecompiledheaders.h"

#include "daeexport.h"

//colladadom includes
#include "dae.h"
#include "dom/domCOLLADA.h"
#include "dom/domMatrix.h"

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

// Files and cache
#include "llcallbacklist.h"
#include "lldir.h"
#include "lltexturecache.h"
#include "llimage.h"
#include "llimagej2c.h"
#include "llimagepng.h"
#include "llimagetga.h"
#include "llimagebmp.h"
#include "llimagejpeg.h"

#include "special_functionality.h"
#include "llmeshrepository.h"
#include "llvolume.h"
#include "llvolumemgr.h"
#include "llvoavatar.h"
#include "llvoavatarself.h"
#include "llcontrolavatar.h"

#define TEXTURE_DOWNLOAD_TIMEOUT 60.0f

extern LLUUID gAgentID;

//Typedefs used in other files, using here for consistency.
typedef LLMemberListener<LLView> view_listener_t;

namespace DAEExportUtil
{
	const auto LL_TEXTURE_PLYWOOD		= LLUUID("89556747-24cb-43ed-920b-47caed15465f");
	const auto LL_TEXTURE_BLANK			= LLUUID("5748decc-f629-461c-9a36-a35a221fe21f");
	const auto LL_TEXTURE_INVISIBLE		= LLUUID("38b86f85-2575-52a9-a531-23108d8da837");
	const auto LL_TEXTURE_TRANSPARENT	= LLUUID("8dcd4a48-2d37-4909-9f78-f7a9eb4ef903");
	const auto LL_TEXTURE_MEDIA			= LLUUID("8b5fec65-8d8d-9dc5-cda8-8fdf2716e361");

	enum image_format_type
	{
		ft_tga,
		ft_png,
		ft_j2c,
		ft_bmp,
		ft_jpg
	};

	static const std::string image_format_ext[] = { "tga", "png", "j2c", "bmp", "jpg" };

	static bool canExportTexture(const LLUUID& id, std::string* name = NULL)
	{
		// Find inventory items with asset id of the sculpt map
		LLViewerInventoryCategory::cat_array_t cats;
		LLViewerInventoryItem::item_array_t items;
		LLAssetIDMatches asset_id_matches(id);
		gInventory.collectDescendentsIf(LLUUID::null,
			cats,
			items,
			LLInventoryModel::INCLUDE_TRASH,
			asset_id_matches);

		// See if any of the inventory items matching this texture id are exportable
		ExportPolicy policy = LFSimFeatureHandler::instance().exportPolicy();
		for (const auto& item : items)
		{
			const LLPermissions item_permissions = item->getPermissions();
			if (gTKOEnableSpecialFunctionality || item_permissions.allowExportBy(gAgentID, policy))
			{
				if (name) *name = item->getName();
				return true;
			}
		}

		if (name) *name = id.getString();

		return gTKOEnableSpecialFunctionality || (policy & ep_full_perm) == ep_full_perm;
	}

	static bool canExportObject(const LLViewerObject* object)
	{
		if (gTKOEnableSpecialFunctionality)
			return true;

		if (object->isSculpted() && !object->isMesh())
		{
			const LLSculptParams* sculpt_params = object->getSculptParams();
			LLUUID sculpt_id = sculpt_params->getSculptTexture();
			return canExportTexture(sculpt_id);
		}
		else
		{
			return true;
		}
	}

	static bool canExportNode(LLSelectNode* node)
	{
        if (gTKOEnableSpecialFunctionality)
            return true;

		LLPermissions* perms = node->mPermissions;	// Is perms ever NULL?
		// This tests the PERM_EXPORT bit too, which is not really necessary (just checking if it's set
		// on the root prim would suffice), but also isn't hurting.
		if (!(perms && perms->allowExportBy(gAgentID, LFSimFeatureHandler::instance().exportPolicy())))
		{
			return false;
		}

		// We already checked generic permissions
		// Additionally chack if this is a sculpt or mesh
		return canExportObject(node->getObject());
	}
}


class ColladaExportFloater
	: public LLFloater
{
private:
	typedef std::map<LLUUID, std::string> texture_list_t;
	LLView* mExportBtn;
	LLView* mFileName;
	LLView* mTextureTypeCombo;
	LLView* mExportRiggedMesh;
	LLView* mResetBindPose;
	LLView* mApplyTextureParams;
	LLView* mConsolidateFaces;

	DAESaver mSaver;
	texture_list_t mTexturesToSave;
	S32 mTotal;
	S32 mNumTextures;
	S32 mNumExportableTextures;
	std::string mObjectName;
	LLTimer mTimer;
	LLUIString mTitleProgress;

public:
	ColladaExportFloater()
		: LLFloater(std::string("Collada Export"), std::string("ColladaExportFloaterRect"), LLStringUtil::null)
	{
		mCommitCallbackRegistrar.add("ColladaExport.FilePicker",		boost::bind(&ColladaExportFloater::onClickBrowse, this));
		mCommitCallbackRegistrar.add("ColladaExport.Export",			boost::bind(&ColladaExportFloater::onClickExport, this));
		mCommitCallbackRegistrar.add("ColladaExport.TextureTypeCombo",	boost::bind(&ColladaExportFloater::onTextureTypeCombo, this, boost::bind(&LLUICtrl::getControlName, _1), _2));
		mCommitCallbackRegistrar.add("ColladaExport.TextureExport",		boost::bind(&ColladaExportFloater::onTextureExportCheck, this, _2));
		mCommitCallbackRegistrar.add("ColladaExport.ExportRiggedMesh",	boost::bind(&ColladaExportFloater::HandleExportRiggedMeshCheck, this, _2));

		LLUICtrlFactory::getInstance()->buildFloater(this, "floater_dae_export.xml");
	}

	virtual ~ColladaExportFloater()
	{
		if (gIdleCallbacks.containsFunction(saveTexturesWorker, this))
		{
			gIdleCallbacks.deleteFunction(saveTexturesWorker, this);
		}
	}

	BOOL postBuild() override
	{
		mFileName				= getChildView("file name editor");
		mExportBtn				= getChildView("export button");
		mTextureTypeCombo       = getChildView("texture type combo");
		mExportRiggedMesh		= getChildView("export rigged mesh");
		mResetBindPose          = getChildView("reset bind pose");
		mApplyTextureParams     = getChildView("texture params check");
		mConsolidateFaces		= getChildView("consolidate check");
		mTitleProgress			= getString("texture_progress");

		mTextureTypeCombo->setValue(gSavedSettings.getS32(mTextureTypeCombo->getControlName()));
		mExportRiggedMesh->setValue(gSavedSettings.getBOOL(mExportRiggedMesh->getControlName()));
		mResetBindPose->setValue(gSavedSettings.getBOOL(mResetBindPose->getControlName()));

		onTextureExportCheck(getChildView("texture export check")->getValue());
		HandleExportRiggedMeshCheck(mExportRiggedMesh->getValue());

		return TRUE;
	}

	void updateOverview()
	{
		if (LLUICtrl* ctrl = findChild<LLUICtrl>("Object Name"))
		{
			ctrl->setTextArg("[NAME]", mObjectName);
		}
		if (LLUICtrl* ctrl = findChild<LLUICtrl>("Exportable Prims"))
		{
			ctrl->setTextArg("[COUNT]", llformat("%d", mSaver.mObjects.size()));
			ctrl->setTextArg("[TOTAL]", llformat("%d", mTotal));
		}
		if (LLUICtrl* ctrl = findChild<LLUICtrl>("Exportable Textures"))
		{
			ctrl->setTextArg("[COUNT]", llformat("%d", mNumExportableTextures));
			ctrl->setTextArg("[TOTAL]", llformat("%d", mNumTextures));
		}
		addTexturePreview();
	}

	void updateTitleProgress()
	{
		mTitleProgress.setArg("COUNT", llformat("%d", mTexturesToSave.size()));
		setTitle(mTitleProgress);
	}

	void onTextureExportCheck(const LLSD& value)
	{
		mTextureTypeCombo->setEnabled(value);
	}

	void HandleExportRiggedMeshCheck(const LLSD& value)
	{
		//mApplyTextureParams->setEnabled(!value);
		//mConsolidateFaces->setEnabled(!value);
		mResetBindPose->setEnabled(value);
	}

	void onTextureTypeCombo(const std::string& control_name, const LLSD& value)
	{
		gSavedSettings.setS32(control_name, value);
	}

	void onClickBrowse()
	{
		static const std::string file_ext = ".dae";
		AIFilePicker* filepicker = AIFilePicker::create();
		filepicker->open(mObjectName + file_ext);
		filepicker->run(boost::bind(&ColladaExportFloater::onFilePicker, this, filepicker));
	}

	void onFilePicker(AIFilePicker* filepicker)
	{
		if (filepicker->hasFilename())
		{
			mFileName->setValue(filepicker->getFilename());
			mExportBtn->setEnabled(TRUE);
		}
	}

	void onClickExport()
	{
		if (gSavedSettings.getBOOL("DAEExportTextures"))
		{
			saveTextures();
		}
		else
		{
			onTexturesSaved();
		}
	}

	void onTexturesSaved()
	{
		std::string selected_filename = mFileName->getValue();
		mSaver.saveDAE(selected_filename);
		LLNotificationsUtil::add("WavefrontExportSuccess", LLSD().with("FILENAME", selected_filename));
		close();
	}

	bool addSelectedObjects()
	{
		LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();

		if (selection && selection->getFirstRootObject())
		{
			mSaver.mRootWorldInvMatrix = LLMatrix4(selection->getFirstRootObject()->getRenderMatrix().getF32ptr());
			mSaver.mRootWorldInvMatrix.invert();

			mObjectName = selection->getFirstRootNode()->mName;
			mTotal = 0;

			for (LLObjectSelection::iterator iter = selection->begin(); iter != selection->end(); ++iter)
			{
				mTotal++;
				LLSelectNode* node = *iter;
				if (!node->getObject()->getVolume() || !DAEExportUtil::canExportNode(node))
					continue;
				mSaver.add(node->getObject(), node->mName);
			}
		}

		if (mSaver.mObjects.empty())
			return false;

		mSaver.updateTextureInfo();
		mNumTextures = mSaver.mTextures.size();
		mNumExportableTextures = getNumExportableTextures();
		return true;
	}

private:
	void try_add_object(const LLViewerObject *object)
	{
		if (object->getVolume() && DAEExportUtil::canExportObject(object))
		{
			LLNameValue* title = object->getNVPair("Title");
			std::string object_name;
			if (title)
				object_name = title->getString();
			else
				object_name = "Object";
			mSaver.add(object, object_name);
		}
	}
public:
	bool addSelectedAvatar()
	{
		LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
		if (selection)
		{
			LLViewerObject* primary_object = selection->getPrimaryObject();
			if (primary_object && primary_object->isAvatar())
			{
				LLVOAvatar* avatar = (LLVOAvatar*)primary_object;

				mSaver.mRootWorldInvMatrix = LLMatrix4(avatar->getRenderMatrix().getF32ptr());
				mSaver.mRootWorldInvMatrix.invert();
				mObjectName = avatar->getFullname();
				mTotal = 0;

				for (LLVOAvatar::joint_attachment_pair_vector_t::const_iterator iter = avatar->mAttachedObjectsVector.begin();
					iter != avatar->mAttachedObjectsVector.end();
					++iter)
				{
					LLViewerObject* object = iter->first;

					// Completely ignore/skip over HUD attachments
					if (object->isHUDAttachment())
						continue;

					mTotal++;
					try_add_object(object);
					LLViewerObject::const_child_list_t& children = object->getChildren();
					for (LLViewerObject::const_child_list_t::const_iterator child_iter = children.begin();
						child_iter != children.end();
						++child_iter)
					{
						mTotal++;
						try_add_object(*child_iter);
					}
				}
			}
			else
			{
				return addSelectedObjects();
			}
		}

		if (mSaver.mObjects.empty())
			return false;

		mSaver.updateTextureInfo();
		mNumTextures = mSaver.mTextures.size();
		mNumExportableTextures = getNumExportableTextures();

		return true;
	}

	S32 getNumExportableTextures()
	{
		S32 res = 0;
		for (const auto& name : mSaver.mTextureNames)
		{
			if (!name.empty()) ++res;
		}

		return res;
	}

	void addTexturePreview()
	{
		S32 num_text = mNumExportableTextures;
		if (num_text == 0) return;

		S32 img_width = 100;
		S32 img_height = img_width + 15;
		S32 panel_height = (num_text / 3 + 1) * (img_height) + 10;
		LLScrollContainer* scroll_container = getChild<LLScrollContainer>("textures container");
		LLPanel* panel = new LLPanel(std::string(), LLRect(0, panel_height, 350, 0), false);
		scroll_container->setScrolledView(panel);
		scroll_container->addChild(panel);
		panel->setEnabled(FALSE);
		S32 img_nr = 0;
		for (U32 i=0; i < mSaver.mTextures.size(); i++)
		{
			if (mSaver.mTextureNames[i].empty()) continue;

			S32 left = 16 + (img_nr % 3) * (img_width + 13);
			S32 bottom = panel_height - (10 + (img_nr / 3 + 1) * (img_height));

			LLRect rect(left, bottom + img_height, left + img_width, bottom);
			LLTextureCtrl* img = new LLTextureCtrl(std::string(), rect, std::string(), mSaver.mTextures[i], LLUUID(), std::string());
			panel->addChild(img);
			img_nr++;
		}
	}

	void saveTextures()
	{
		mTexturesToSave.clear();
		for (U32 i=0; i < mSaver.mTextures.size(); i++)
		{
			if (mSaver.mTextureNames[i].empty()) continue;
			mTexturesToSave[mSaver.mTextures[i]] = mSaver.mTextureNames[i];
		}

		mSaver.mImageFormat = DAEExportUtil::image_format_ext[(S32)mTextureTypeCombo->getValue()];

		LL_INFOS("ColladaExport") << "Starting to save textures" << LL_ENDL;
		mTimer.setTimerExpirySec(TEXTURE_DOWNLOAD_TIMEOUT);
		mTimer.start();
		updateTitleProgress();
		gIdleCallbacks.addFunction(saveTexturesWorker, this);
	}

	class CacheReadResponder final : public LLTextureCache::ReadResponder
	{
	private:
		LLPointer<LLImageFormatted> mFormattedImage;
		LLUUID mID;
		std::string mName;
		S32 mImageType;

	public:
		CacheReadResponder(const LLUUID& id, LLImageFormatted* image, std::string name, S32 img_type)
			: mFormattedImage(image), mID(id), mName(name), mImageType(img_type)
		{
			setImage(image);
		}

		void setData(U8* data, S32 datasize, S32 imagesize, S32 imageformat, BOOL imagelocal)
		{
			if (imageformat == IMG_CODEC_TGA && mFormattedImage->getCodec() == IMG_CODEC_J2C)
			{
				LL_WARNS("ColladaExport") << "FAILED: texture " << mID << " is formatted as TGA. Not saving." << LL_ENDL;
				mFormattedImage = NULL;
				mImageSize = 0;
				return;
			}

			if (mFormattedImage.notNull())
			{	
				if (mFormattedImage->getCodec() == imageformat)
				{
					mFormattedImage->appendData(data, datasize);
				}
				else 
				{
					LL_WARNS("ColladaExport") << "FAILED: texture " << mID << " in wrong format." << LL_ENDL;
					mFormattedImage = NULL;
					mImageSize = 0;
					return;
				}
			}
			else
			{
				mFormattedImage = LLImageFormatted::createFromType(imageformat);
				mFormattedImage->setData(data, datasize);
			}
			mImageSize = imagesize;
			mImageLocal = imagelocal;
		}

		void completed(bool success) override
		{
			if (success && mFormattedImage.notNull() && mImageSize > 0)
			{
				bool ok = false;

				// If we are saving jpeg2000, no need to do anything, just write to disk
				if (mImageType == DAEExportUtil::ft_j2c)
				{
					mName += "." + mFormattedImage->getExtension();
					ok = mFormattedImage->save(mName);
				}
				else
				{
					// For other formats we need to decode first
					if (mFormattedImage->updateData() && (mFormattedImage->getWidth() * mFormattedImage->getHeight() * mFormattedImage->getComponents()))
					{
						LLPointer<LLImageRaw> raw = new LLImageRaw;
						raw->resize(mFormattedImage->getWidth(), mFormattedImage->getHeight(),	mFormattedImage->getComponents());

						if (mFormattedImage->decode(raw, 0))
						{
							LLPointer<LLImageFormatted> img = NULL;
							switch (mImageType)
							{
							case DAEExportUtil::ft_tga:
								img = new LLImageTGA;
								break;
							case DAEExportUtil::ft_png:
								img = new LLImagePNG;
								break;
							case DAEExportUtil::ft_bmp:
								img = new LLImageBMP;
								break;
							case DAEExportUtil::ft_jpg:
								img = new LLImageJPEG;
								break;
							}

							if (!img.isNull())
							{
								if (img->encode(raw, 0))
								{
									mName += "." + img->getExtension();
									ok = img->save(mName);
								}
							}
						}
					}
				}

				if (ok)
				{
					LL_INFOS("ColladaExport") << "Saved texture to " << mName << LL_ENDL;
				}
				else
				{
					LL_WARNS("ColladaExport") << "FAILED to save texture " << mID << LL_ENDL;
				}
			}
			else
			{
				LL_WARNS("ColladaExport") << "FAILED to save texture " << mID << LL_ENDL;
			}
		}
	};

	static void saveTexturesWorker(void* data)
	{
		ColladaExportFloater* me = (ColladaExportFloater *)data;
		if (me->mTexturesToSave.size() == 0)
		{
			LL_INFOS("ColladaExport") << "Done saving textures" << LL_ENDL;
			me->updateTitleProgress();
			gIdleCallbacks.deleteFunction(saveTexturesWorker, me);
			me->mTimer.stop();
			me->onTexturesSaved();
			return;
		}

		LLUUID id = me->mTexturesToSave.begin()->first;
		LLViewerTexture* imagep = LLViewerTextureManager::findFetchedTexture(id, TEX_LIST_STANDARD);
		if (!imagep)
		{
			me->mTexturesToSave.erase(id);
			me->updateTitleProgress();
			me->mTimer.reset();
		}
		else
		{
			if (imagep->getDiscardLevel() == 0) // image download is complete
			{
				LL_INFOS("ColladaExport") << "Saving texture " << id << LL_ENDL;
				LLImageFormatted* img = new LLImageJ2C;
				S32 img_type = me->mTextureTypeCombo->getValue();
				std::string name = gDirUtilp->getDirName(me->mFileName->getValue());
				name += gDirUtilp->getDirDelimiter() + me->mTexturesToSave[id];
				CacheReadResponder* responder = new CacheReadResponder(id, img, name, img_type);
				LLAppViewer::getTextureCache()->readFromCache(id, LLWorkerThread::PRIORITY_HIGH, 0, 999999, responder);
				me->mTexturesToSave.erase(id);
				me->updateTitleProgress();
				me->mTimer.reset();
			}
			else if (me->mTimer.hasExpired())
			{
				LL_WARNS("ColladaExport") << "Timed out downloading texture " << id << LL_ENDL;
				me->mTexturesToSave.erase(id);
				me->updateTitleProgress();
				me->mTimer.reset();
			}
		}
	}

};

void DAESaver::add(const LLViewerObject* prim, const std::string name)
{
	mObjects.push_back(std::pair<LLViewerObject*,std::string>((LLViewerObject*)prim, name));
}

void DAESaver::updateTextureInfo()
{
	mTextures.clear();
	mTextureNames.clear();

	for (obj_info_t::iterator obj_iter = mObjects.begin(); obj_iter != mObjects.end(); ++obj_iter)
	{
		LLViewerObject* obj = obj_iter->first;
		S32 num_faces = obj->getVolume()->getNumVolumeFaces();
		for (S32 face_num = 0; face_num < num_faces; ++face_num)
		{
			LLTextureEntry* te = obj->getTE(face_num);
			uuid_vec_t candidates;

			const LLUUID id_color = te->getID();
			candidates.push_back(id_color);

			const LLMaterialPtr materials = te->getMaterialParams();
			if (materials)
			{
				const LLUUID id_normal = materials->getNormalID();
				const LLUUID id_specular = materials->getSpecularID();
				candidates.push_back(id_normal);
				candidates.push_back(id_specular);
			}

			for (const LLUUID id : candidates)
			{
				if (std::find(mTextures.begin(), mTextures.end(), id) != mTextures.end()) continue;

				mTextures.push_back(id);
				std::string name;
				if (id != DAEExportUtil::LL_TEXTURE_BLANK && DAEExportUtil::canExportTexture(id, &name))
				{
					std::string safe_name = gDirUtilp->getScrubbedFileName(name);
					std::replace(safe_name.begin(), safe_name.end(), ' ', '_');
					mTextureNames.push_back(safe_name);
				}
				else
				{
					mTextureNames.push_back(std::string());
				}
			}
		}
	}
}

class v4adaptbase
{
protected:
	LLStrider<LLVector4a> mV4aStrider;
	v4adaptbase(LLVector4a* vp) { mV4aStrider = vp; }
};

class v4adapt3 : v4adaptbase
{
public:
	v4adapt3(LLVector4a* vp) : v4adaptbase(vp) { }
	inline LLVector3 operator[] (const unsigned int i)
	{
		return LLVector3((F32*)&mV4aStrider[i]);
	}
};

class v4adapt4 : v4adaptbase
{
public:
	v4adapt4(LLVector4a* vp) : v4adaptbase(vp) { }
	inline LLVector4 operator[] (const unsigned int i)
	{
		return LLVector4((F32*)&mV4aStrider[i]);
	}
};

void DAESaver::addSourceParams(daeElement* mesh, const char* src_id, const std::string& params, const std::vector<F32> &vals)
{
	daeElement* source = mesh->add("source");
	source->setAttribute("id", src_id);
	daeElement* src_array = source->add("float_array");

	src_array->setAttribute("id", llformat("%s-%s", src_id, "array").c_str());
	src_array->setAttribute("count", llformat("%d", vals.size()).c_str());

	for (const auto& val : vals)
	{
		static_cast<domFloat_array*>(src_array)->getValue().append(val);
	}

	domAccessor* acc = daeSafeCast<domAccessor>(source->add("technique_common accessor"));
	acc->setSource(llformat("#%s-%s", src_id, "array").c_str());
	acc->setCount(vals.size() / params.size());
	acc->setStride(params.size());

	for (const auto& param : params)
	{
		domElement* pX = acc->add("param");
		pX->setAttribute("name", (LLStringUtil::null + param).c_str());
		pX->setAttribute("type", "float");
	}
}

void DAESaver::addSource(daeElement* mesh, const char* src_id, const char* param_name, const std::vector<F32>& vals)
{
	daeElement* source = mesh->add("source");
	source->setAttribute("id", src_id);
	daeElement* src_array = source->add("float_array");

	src_array->setAttribute("id", llformat("%s-%s", src_id, "array").c_str());
	src_array->setAttribute("count", llformat("%d", vals.size()).c_str());

	for (U32 i = 0; i < vals.size(); i++)
	{
		((domFloat_array*)src_array)->getValue().append(vals[i]);
	}

	domAccessor* acc = daeSafeCast<domAccessor>(source->add("technique_common accessor"));
	acc->setSource(llformat("#%s-%s", src_id, "array").c_str());
	acc->setCount(vals.size());
	domElement* pX = acc->add("param");
	pX->setAttribute("name", param_name);
	pX->setAttribute("type", "float");
}

void DAESaver::addSource(daeElement* mesh, const char* src_id, const char* param_name, const std::vector<std::string>& vals)
{
	daeElement* source = mesh->add("source");
	source->setAttribute("id", src_id);
	daeElement* src_array = source->add("Name_array");

	src_array->setAttribute("id", llformat("%s-%s", src_id, "array").c_str());
	src_array->setAttribute("count", llformat("%d", vals.size()).c_str());

	for (U32 i = 0; i < vals.size(); i++)
	{
		((domName_array*)src_array)->getValue().append(vals[i].c_str());
	}

	domAccessor* acc = daeSafeCast<domAccessor>(source->add("technique_common accessor"));
	acc->setSource(llformat("#%s-%s", src_id, "array").c_str());
	acc->setCount(vals.size());
	domElement* pX = acc->add("param");
	pX->setAttribute("name", param_name);
	pX->setAttribute("type", "name");
}

void DAESaver::append(daeTArray<domFloat>& arr, const LLMatrix4& matrix)
{
	for (int i = 0; i < 16; i++)
		arr.append(matrix.mMatrix[i % 4][i / 4]);
}

void DAESaver::addSource(daeElement* parent, const char* src_id, const char* param_name, const std::vector<LLMatrix4>& vals)
{
	daeElement* source = parent->add("source");
	source->setAttribute("id", src_id);
	daeElement* src_array = source->add("float_array");
	size_t array_size = 16 * vals.size();

	src_array->setAttribute("id", llformat("%s-%s", src_id, "array").c_str());
	src_array->setAttribute("count", llformat("%d", array_size).c_str());

    // Copy matrix values (rows & columns) into source array
	for (std::vector<LLMatrix4>::const_iterator mat_iter = vals.begin(); mat_iter != vals.end(); ++mat_iter)
		append(((domFloat_array*)src_array)->getValue(), *mat_iter);

	domAccessor* acc = daeSafeCast<domAccessor>(source->add("technique_common accessor"));
	acc->setSource(llformat("#%s-%s", src_id, "array").c_str());
	acc->setCount(vals.size());
	acc->setStride(16);

	domElement* pX = acc->add("param");
	pX->setAttribute("name", param_name);
	pX->setAttribute("type", "float4x4");
}

void DAESaver::addPolygons(daeElement* mesh, const char* geomID, const char* materialID, LLViewerObject* obj, int_list_t* faces_to_include)
{
	domPolylist* polylist = daeSafeCast<domPolylist>(mesh->add("polylist"));
	polylist->setMaterial(materialID);

	// Vertices semantic
	{
		domInputLocalOffset* input = daeSafeCast<domInputLocalOffset>(polylist->add("input"));
		input->setSemantic("VERTEX");
		input->setOffset(0);
		input->setSource(llformat("#%s-%s", geomID, "vertices").c_str());
	}

	// Normals semantic
	{
		domInputLocalOffset* input = daeSafeCast<domInputLocalOffset>(polylist->add("input"));
		input->setSemantic("NORMAL");
		input->setOffset(0);
		input->setSource(llformat("#%s-%s", geomID, "normals").c_str());
	}

	// UV semantic
	{
		domInputLocalOffset* input = daeSafeCast<domInputLocalOffset>(polylist->add("input"));
		input->setSemantic("TEXCOORD");
		input->setOffset(0);
		input->setSource(llformat("#%s-%s", geomID, "map0").c_str());
	}

	// Save indices
	domP* p = daeSafeCast<domP>(polylist->add("p"));
	domPolylist::domVcount *vcount = daeSafeCast<domPolylist::domVcount>(polylist->add("vcount"));
	S32 index_offset = 0;
	S32 num_tris = 0;
	for (S32 face_num = 0; face_num < obj->getVolume()->getNumVolumeFaces(); face_num++)
	{
		if (skipFace(obj->getTE(face_num))) continue;

		const LLVolumeFace* face = (LLVolumeFace*)&obj->getVolume()->getVolumeFace(face_num);

		if (faces_to_include == NULL || (std::find(faces_to_include->begin(), faces_to_include->end(), face_num) != faces_to_include->end()))
		{
			for (S32 i = 0; i < face->mNumIndices; i++)
			{
				U32 index = index_offset + face->mIndices[i];
				(p->getValue()).append(index);
				if (i % 3 == 0)
				{
					(vcount->getValue()).append(3);
					num_tris++;
				}
			}
		}
		index_offset += face->mNumVertices;
	}
	polylist->setCount(num_tris);
}

void DAESaver::addJointsAndWeights(daeElement* skin, const char* parent_id, LLViewerObject* obj, int_list_t* faces_to_include)
{
	std::string joints_source_id = llformat("%s-%s", parent_id, "joints");
	std::string skin_weights_source_id = llformat("%s-%s", parent_id, "weights");
	std::string bind_pose_source_id = llformat("%s-%s", parent_id, "bind_poses");

	domSkin::domJoints* joints = daeSafeCast<domSkin::domJoints>(skin->add("joints"));
	domSkin::domVertex_weights* vertex_weights = daeSafeCast<domSkin::domVertex_weights>(skin->add("vertex_weights"));

	domInputLocal* joints_input = (domInputLocal*)joints->add("input");
	joints_input->setSemantic("JOINT");
	joints_input->setSource(("#" + joints_source_id).c_str());

	domInputLocal* inv_bind_mtx_input = daeSafeCast<domInputLocal>(joints->add("input"));
	inv_bind_mtx_input->setSemantic("INV_BIND_MATRIX");
	inv_bind_mtx_input->setSource(("#" + bind_pose_source_id).c_str());

	domInputLocalOffset* vw_joints_input = daeSafeCast<domInputLocalOffset>(vertex_weights->add("input"));
	vw_joints_input->setAttribute("offset", "0");
	vw_joints_input->setSemantic("JOINT");
	vw_joints_input->setSource(("#" + joints_source_id).c_str());

	domInputLocalOffset* vw_weights_input = daeSafeCast<domInputLocalOffset>(vertex_weights->add("input"));
	vw_weights_input->setAttribute("offset", "1");
	vw_weights_input->setSemantic("WEIGHT");
	vw_weights_input->setSource(("#" + skin_weights_source_id).c_str());

	domSkin::domVertex_weights::domV* v_array = daeSafeCast<domSkin::domVertex_weights::domV>(vertex_weights->add("v"));
	domListOfInts& v_array_list = v_array->getValue();
	domSkin::domVertex_weights::domVcount* vcounts = daeSafeCast<domSkin::domVertex_weights::domVcount>(vertex_weights->add("vcount"));
	domListOfUInts& vcounts_list = vcounts->getValue();

	std::vector<F32> weights_list;

	for (S32 face_num = 0; face_num < obj->getVolume()->getNumVolumeFaces(); face_num++)
	{
		if (skipFace(obj->getTE(face_num)))
			continue;

		const LLVolumeFace* face = (LLVolumeFace*)&obj->getVolume()->getVolumeFace(face_num);

		if (faces_to_include == NULL || (std::find(faces_to_include->begin(), faces_to_include->end(), face_num) != faces_to_include->end()))
		{
			for (S32 i = 0; i < face->mNumVertices; i++)
			{
				LLVector4a w = face->mWeights[i];
				S32 vcount = 0;
				for (S32 c = 0; c < 4; c++)
				{
					S32 joint_idx = (S32)w[c];
					F32 amount = w[c] - (F32)joint_idx;
					if (amount > 0.0f)
					{
						v_array_list.append(joint_idx);
						v_array_list.append(weights_list.size());
						weights_list.push_back(amount);
						vcount++;
					}
				}
				vcounts_list.append(vcount);
			}
		}
	}

	addSource(skin, skin_weights_source_id.c_str(), "WEIGHT", (const std::vector<F32>&)weights_list);
	vertex_weights->setCount(vcounts_list.getCount());
}

void DAESaver::addJointNodes(daeElement* parent, LLJoint* joint, LLVector3 joint_parent_scale)
{
	// Set up joint node
	domNode* root_node = daeSafeCast<domNode>(parent->add("node"));
	const char* name = joint->getName().c_str();
	root_node->setId(name);
	root_node->setSid(name);
	root_node->setName(name);
	root_node->setType(domNodeType::NODETYPE_JOINT);

	// Add transform matrix element to joint node
	domMatrix* mtx_elem = daeSafeCast<domMatrix>(root_node->add("matrix"));
	mtx_elem->setSid("transform");

	// Set (local) transform matrix for current joint
	LLMatrix4 joint_mtx;
	LLVector3 local_position, local_scale;

	auto has_attachment_overrides = false;
	if (!joint->hasAttachmentPosOverride(local_position, LLUUID()))
	{
		// Should this use LLAvatarJoint::getSkinOffset()?
		local_position = joint->getPosition();
		has_attachment_overrides = true;
	}

	if (!joint->hasAttachmentScaleOverride(local_scale, LLUUID()))
	{
		local_scale = joint->getScale();
		has_attachment_overrides = true;
	}

	// Calculate "inverse" of parent scale
	auto parent_inv_scale = LLVector3(
		1.f / joint_parent_scale[VX],
		1.f / joint_parent_scale[VY],
		1.f / joint_parent_scale[VZ]);


	// TODO: take into account AvatarJoint::inheritScale() == true
	// Apply inverse of parent scale to local scale
	auto scale = local_scale.scaledVec(parent_inv_scale);
	//auto position = local_position.scaledVec(parent_inv_scale);
	auto position = local_position;

	// Only use rotation if this joint is an animation joint (i.e. not a collision volume or attachment point)
	// This is really dirty but I don't want to patch llavatarjoint just for this.
	auto is_collision_volume = dynamic_cast<LLAvatarJointCollisionVolume*>(joint) != NULL;
	auto rotation = is_collision_volume ? joint->getRotation() : LLQuaternion();

	// Assume identity rotation for joint matrix?
	joint_mtx.initAll(scale, rotation, position);

	// Write joint matrix into DOM element value
	append(mtx_elem->getValue(), joint_mtx);

	// Recurse over child joints
	for (LLJoint::child_list_t::const_iterator iter = joint->mChildren.begin(); iter != joint->mChildren.end(); ++iter)
	{
		// Use local scale for parent scale
		// To not apply parent inverse scale to this
		addJointNodes(root_node, *iter, local_scale);
	}
}

void DAESaver::transformTexCoord(S32 num_vert, LLVector2* coord, LLVector3* positions, LLVector3* normals, LLTextureEntry* te, LLVector3 scale)
{
	F32 cosineAngle = cos(te->getRotation());
	F32 sinAngle = sin(te->getRotation());

	for (S32 ii=0; ii<num_vert; ii++)
	{
		if (LLTextureEntry::TEX_GEN_PLANAR == te->getTexGen())
		{
			LLVector3 normal = normals[ii];
			LLVector3 pos = positions[ii];
			LLVector3 binormal;
			F32 d = normal * LLVector3::x_axis;
			if (d >= 0.5f || d <= -0.5f)
			{
				binormal = LLVector3::y_axis;
				if (normal.mV[0] < 0) binormal *= -1.0f;
			}
			else
			{
				binormal = LLVector3::x_axis;
				if (normal.mV[1] > 0) binormal *= -1.0f;
			}
			LLVector3 tangent = binormal % normal;
			LLVector3 scaledPos = pos.scaledVec(scale);
			coord[ii].mV[0] = 1.f + ((binormal * scaledPos) * 2.f - 0.5f);
			coord[ii].mV[1] = -((tangent * scaledPos) * 2.f - 0.5f);
		}

		F32 repeatU;
		F32 repeatV;
		te->getScale(&repeatU, &repeatV);
		F32 tX = coord[ii].mV[0] - 0.5f;
		F32 tY = coord[ii].mV[1] - 0.5f;

		F32 offsetU;
		F32 offsetV;
		te->getOffset(&offsetU, &offsetV);

		coord[ii].mV[0] = (tX * cosineAngle + tY * sinAngle) * repeatU + offsetU + 0.5f;
		coord[ii].mV[1] = (-tX * sinAngle + tY * cosineAngle) * repeatV + offsetV + 0.5f;
	}
}

LLMatrix4 getJointMatrixForAttachment(LLJoint* joint)
{
	LLMatrix4 ret_mtx;
	LLJoint
		*root = joint->getRoot(),
		*parent_joint = joint->getParent();
	for (; joint != NULL && joint != root; joint = parent_joint)
	{
		LLVector3 position;

		if (!joint->hasAttachmentPosOverride(position, LLUUID()))
			position = joint->getPosition();

		parent_joint = joint->getParent();

		ret_mtx.rotate(joint->getRotation());

		if (parent_joint)
		{
			LLVector3 parent_scale;
			if (!parent_joint->hasAttachmentScaleOverride(parent_scale, LLUUID()))
				parent_scale = parent_joint->getScale();
			ret_mtx.translate(position.scaledVec(parent_scale));
		}
		else
		{
			ret_mtx.translate(position);
		}
	}

	return ret_mtx;
}

// Note: does not apply scale transform from parents of xform
LLMatrix4 getRelativeMatrix(LLXform* root, LLXform* xform)
{
	LLMatrix4 ret_mtx;
	xform->getLocalMat4(ret_mtx);
	for (xform = xform->getParent(); xform != NULL && xform != root; xform = xform->getParent())
	{
		ret_mtx.rotate(xform->getRotation());
		ret_mtx.translate(xform->getPosition());
	}
	return ret_mtx;
}

void getMatrixRotScale(LLMatrix3 mtx, LLQuaternion& rotation, LLVector3& scale)
{
	// Before we orthagonalize the matrix, let's calculate its scale vector
	scale = LLVector3(
		mtx.getFwdRow().magVec(),
		mtx.getLeftRow().magVec(),
		mtx.getUpRow().magVec());
	// In order to extract the rotation, we need an orthagonal matrix
	mtx.orthogonalize();
	rotation = LLQuaternion(mtx);
}

// Assumes that all components are nonzero
LLVector3 getVector3Reciprocal(const LLVector3& vector)
{
	return LLVector3(
		1.f / vector.mV[VX],
		1.f / vector.mV[VY],
		1.f / vector.mV[VZ]);
}

bool DAESaver::saveDAE(std::string filename)
{
	// Collada expects file and folder names to be escaped
	// Note: cdom::nativePathToUri()
	// Same as in LLDAELoader::OpenFile()
	const char* allowed =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789"
		"%-._~:\"|\\/";
	std::string uri_filename = LLURI::escape(filename, allowed);

	mAllMaterials.clear();
	mTotalNumMaterials = 0;
	DAE dae;
	// First set the filename to save
	daeElement* root = dae.add(uri_filename);

	// Obligatory elements in header
	daeElement* asset = root->add("asset");
	// Get ISO format time
	time_t rawtime;
	time(&rawtime);
	struct tm* utc_time = gmtime(&rawtime);
	std::string date = llformat("%04d-%02d-%02dT%02d:%02d:%02d", utc_time->tm_year + 1900, utc_time->tm_mon + 1, utc_time->tm_mday, utc_time->tm_hour, utc_time->tm_min, utc_time->tm_sec);
	daeElement* created = asset->add("created");
	created->setCharData(date);
	daeElement* modified = asset->add("modified");
	modified->setCharData(date);
	daeElement* unit = asset->add("unit");
	unit->setAttribute("name", "meter");
	unit->setAttribute("value", "1");
	daeElement* up_axis = asset->add("up_axis");
	up_axis->setCharData("Z_UP");

	// File creator
	std::string author;
	if (!LLAvatarNameCache::getNSName(gAgentID, author))
		author = "Unknown";
	
	daeElement* contributor = asset->add("contributor");
	contributor->add("author")->setCharData(author);
	contributor->add("authoring_tool")->setCharData(LLVersionInfo::getChannelAndVersion() + " Collada Export");

	const bool export_rigged_mesh = gSavedSettings.getBOOL("DAEExportRiggedMesh");
	const bool export_consolidate_materials = gSavedSettings.getBOOL("DAEExportConsolidateMaterials");

	daeElement* images = root->add("library_images");
	daeElement* geomLib = root->add("library_geometries");
	daeElement* effects = root->add("library_effects");
	daeElement* materials = root->add("library_materials");

	daeElement* controllersLib = NULL;
	if (export_rigged_mesh)
		controllersLib = root->add("library_controllers");

	daeElement* scene = root->add("library_visual_scenes visual_scene");

	scene->setAttribute("id", "Scene");
	scene->setAttribute("name", "Scene");

	if (gSavedSettings.getBOOL("DAEExportTextures"))
	{
		generateImagesSection(images);
	}

	S32 prim_number = 0;
	const bool applyTexCoord = gSavedSettings.getBOOL("DAEExportTextureParams");

	// Whether or not the avatar nodes have been added for an avatar rig
	bool avatar_node_added = false;
	// Singleton skeleton
	std::string skeleton_source_id;

	// Iterate over objects
	for (obj_info_t::iterator obj_iter = mObjects.begin(); obj_iter != mObjects.end(); ++obj_iter)
	{
		LLViewerObject* const obj = obj_iter->first;
		const std::string obj_name = obj_iter->second;
		const bool obj_is_rigged_mesh = obj->isRiggedMesh();
		LLMatrix4 bind_shape_mtx;
		LLMatrix4 bind_shape_normal_mtx;
		if (export_rigged_mesh && obj_is_rigged_mesh)
		{
			// Cache the object's bind shape matrix to be applied to be applied to vertices later.
			auto obj_vov = (LLVOVolume*)obj;
			auto skin_info = obj_vov->getSkinInfo();

			// Bind shape matrix is pretty simple for vertex positions.
			bind_shape_mtx = skin_info->mBindShapeMatrix;

			// Vertex normals are transformed by an "inverse" scale and (un-inverted) rotation matrix.
			LLQuaternion bind_shape_rotation;
			LLVector3 bind_shape_scale;
			getMatrixRotScale(bind_shape_mtx.getMat3(), bind_shape_rotation, bind_shape_scale);

			// "Un-scale" is the reciprocal of the object's scale
			auto normal_unscale = getVector3Reciprocal(bind_shape_scale);

			bind_shape_normal_mtx.initAll(normal_unscale, bind_shape_rotation, LLVector3::zero);
		}

		S32 total_num_vertices = 0;

		std::string prim_id = llformat("prim%d", prim_number++);
		std::string geom_id = llformat("%s-%s", prim_id, "mesh");

		daeElement* geom = geomLib->add("geometry");
		geom->setAttribute("id", geom_id.c_str());

		daeElement* mesh = geom->add("mesh");

		std::vector<F32> position_data;
		std::vector<F32> normal_data;
		std::vector<F32> uv_data;

		S32 num_faces = obj->getVolume()->getNumVolumeFaces();

		for (S32 face_num = 0; face_num < num_faces; face_num++)
		{
			if (skipFace(obj->getTE(face_num)))
				continue;

			const LLVolumeFace* face = (LLVolumeFace*)&obj->getVolume()->getVolumeFace(face_num);
			total_num_vertices += face->mNumVertices;

			v4adapt3 verts(face->mPositions);
			v4adapt3 norms(face->mNormals);
			//v4adapt4 skin_weights(face->mWeights);

			LLVector2* newCoord = NULL;

			if (applyTexCoord)
			{
				newCoord = new LLVector2[face->mNumVertices];
				LLVector3* newPos = new LLVector3[face->mNumVertices];
				LLVector3* newNormal = new LLVector3[face->mNumVertices];
				for (S32 i = 0; i < face->mNumVertices; i++)
				{
					newPos[i] = verts[i];
					newNormal[i] = norms[i];
					newCoord[i] = face->mTexCoords[i];
				}
				transformTexCoord(face->mNumVertices, newCoord, newPos, newNormal, obj->getTE(face_num), obj->getScale());
				delete[] newPos;
				delete[] newNormal;
			}


			for (S32 i=0; i < face->mNumVertices; i++)
			{
				LLVector3 v = verts[i];
				LLVector3 n = norms[i];
				//LLVector4 w = skin_weights[i];

				// If the object is rigged mesh, apply bind shape matrices to
				// vertex positions and normals
				if (export_rigged_mesh && obj_is_rigged_mesh)
				{
					v = v * bind_shape_mtx;
					n = n * bind_shape_normal_mtx;
					n.normalize();
				}

				position_data.push_back(v.mV[VX]);
				position_data.push_back(v.mV[VY]);
				position_data.push_back(v.mV[VZ]);

				normal_data.push_back(n.mV[VX]);
				normal_data.push_back(n.mV[VY]);
				normal_data.push_back(n.mV[VZ]);

				const LLVector2 uv = applyTexCoord ? newCoord[i] : face->mTexCoords[i];

				uv_data.push_back(uv.mV[VX]);
				uv_data.push_back(uv.mV[VY]);
			}

			if (applyTexCoord)
			{
				delete[] newCoord;
			}
		}

		addSourceParams(mesh, llformat("%s-%s", geom_id, "positions").c_str(), "XYZ", position_data);
		addSourceParams(mesh, llformat("%s-%s", geom_id, "normals").c_str(), "XYZ", normal_data);
		addSourceParams(mesh, llformat("%s-%s", geom_id, "map0").c_str(), "ST", uv_data);

		// Add the <vertices> element
		{
			daeElement*	verticesNode = mesh->add("vertices");
			verticesNode->setAttribute("id", llformat("%s-%s", geom_id, "vertices").c_str());
			daeElement* verticesInput = verticesNode->add("input");
			verticesInput->setAttribute("semantic", "POSITION");
			verticesInput->setAttribute("source", llformat("#%s-%s", geom_id, "positions").c_str());
		}

		material_list_t objMaterials;
		getMaterials(obj, &objMaterials);

		// Add triangles
		int_list_t faces;
		if (export_consolidate_materials)
		{
			for (const auto& objMaterial : objMaterials)
			{
				getFacesWithMaterial(obj, objMaterial, &faces);
				std::string matName = objMaterial.name;
				addPolygons(mesh, geom_id.c_str(), (matName + "-material").c_str(), obj, &faces);
			}
		}
		else
		{
			S32 mat_nr = 0;
			for (S32 face_num = 0; face_num < num_faces; face_num++)
			{
				if (skipFace(obj->getTE(face_num)))
					continue;
				faces.push_back(face_num);
				std::string matName = objMaterials[mat_nr++].name;
				addPolygons(mesh, geom_id.c_str(), (matName + "-material").c_str(), obj, &faces);
			}
		}

		daeElement* node = scene->add("node");
		node->setAttribute("type", "NODE");
		node->setAttribute("id", prim_id.c_str());
		node->setAttribute("name", prim_id.c_str());

		daeElement* nodeInstance;

		domMatrix* matrix_elem = (domMatrix*)node->add("matrix");
		LLMatrix4 node_xform_mtx;

		if (export_rigged_mesh && obj_is_rigged_mesh)
		{
			// Get the skin info
			LLVOVolume* obj_vov = (LLVOVolume*)obj;
			const LLMeshSkinInfo* skin_info = obj_vov->getSkinInfo();

			if (!avatar_node_added)
			{
				// Try to use the avatar the mesh is rigged to
				LLVOAvatar* avatar = obj->getAvatarAncestor();

				// If it is not attached to an avatar, use own avatar for reference
				if (avatar == NULL)
					avatar = gAgentAvatarp;

				domNode* avatar_node = daeSafeCast<domNode>(scene->add("node"));
				avatar_node->setId("Avatar");
				avatar_node->setName("Avatar");
				avatar_node->setType(domNodeType::NODETYPE_NODE);
				LLJoint* root_joint = avatar->mPelvisp; // because yeah
				// Reference mesh id to use for joint position/scale overrides
				//LLUUID mesh_id = skin_info->mMeshID;
				//addJointNodes(avatar_node, root_joint, mesh_id);
				addJointNodes(avatar_node, root_joint);
				skeleton_source_id = root_joint->getName();
				avatar_node_added = true;
			}

			// Add a controller + skin for this rigged mesh
			domController* controller = (domController*)controllersLib->add("controller");
			std::string controller_id = llformat("%s-%s", prim_id, "skin");
			controller->setId(controller_id.c_str());
			domSkin* skin = (domSkin*)controller->add("skin");
			skin->setSource(("#" + geom_id).c_str());

			// Set skin bind shape matrix
			domSkin::domBind_shape_matrix* bind_shape_matrix = daeSafeCast<domSkin::domBind_shape_matrix>(skin->add("bind_shape_matrix"));
			//append(bind_shape_matrix->getValue(), skin_info->mBindShapeMatrix);
			append(bind_shape_matrix->getValue(), LLMatrix4());

			// Add joints name source to skin (as Name_array)
			addSource(skin, llformat("%s-%s", controller_id, "joints").c_str(), "JOINT", skin_info->mJointNames);

			// Add bind poses source to skin
			addSource(skin, llformat("%s-%s", controller_id, "bind_poses").c_str(), "TRANSFORM", skin_info->mInvBindMatrix);

			// Add vertex weight source, joints, and vertex weights
			addJointsAndWeights(skin, controller_id.c_str(), obj, &faces);
			
			// Geometry of the node
			nodeInstance = node->add("instance_controller");
			nodeInstance->setAttribute("url", ("#" + controller_id).c_str());

			domInstance_controller::domSkeleton* skeleton = daeSafeCast<domInstance_controller::domSkeleton>(nodeInstance->add("skeleton"));
			skeleton->setValue(("#" + skeleton_source_id).c_str());

			node_xform_mtx = LLMatrix4(); // Identity
			//node_xform_mtx = skin_info->mBindShapeMatrix;
			//node_xform_mtx.invert();
		}
		else
		{
			// Geometry of the node
			nodeInstance = node->add("instance_geometry");
			nodeInstance->setAttribute("url", ("#" + geom_id).c_str());

			// When exporting rigged mesh, use avatar-reltative space for attachments
			if (obj->isAttachment())
			{
				auto parent = (LLViewerObject*)obj->getParent();
				auto avatar = obj->getAvatarAncestor();
				auto attachment_point = avatar->getTargetAttachmentPoint(obj);
				auto attachment_joint_mtx = getJointMatrixForAttachment(attachment_point);

				// Get Xform matrix between avatar root and object
				node_xform_mtx = getRelativeMatrix(avatar, obj);
				node_xform_mtx *= attachment_joint_mtx;
			}
			else
			{
				// Use world space
				node_xform_mtx.initAll(obj->getScale(), obj->getRenderRotation(), obj->getRenderPosition());

				// Apply root world inverse matrix to get relative position/rotation
				node_xform_mtx *= mRootWorldInvMatrix;
			}
		}

		// Append tranform matrix (node position, rotation, and scale)
		append(matrix_elem->getValue(), node_xform_mtx);

		// Bind materials
		daeElement* tq = nodeInstance->add("bind_material technique_common");
		for (const auto& objMaterial : objMaterials)
		{
			daeElement* instanceMaterial = tq->add("instance_material");
			std::string matName = objMaterial.name + "-material";
			instanceMaterial->setAttribute("symbol", matName.c_str());
			instanceMaterial->setAttribute("target", ('#' + matName).c_str());
		}
	}

	// Effects (face texture, color, alpha)
	generateEffects(effects);

	// Materials
	for (const auto& objMaterial : mAllMaterials)
	{
		daeElement* mat = materials->add("material");
		mat->setAttribute("id", (objMaterial.name + "-material").c_str());
		daeElement* matEffect = mat->add("instance_effect");
		matEffect->setAttribute("url", ('#' + objMaterial.name + "-fx").c_str());
	}

	root->add("scene instance_visual_scene")->setAttribute("url", "#Scene");

	return dae.writeAll();
}

DAESaver::DAESaver()
{}

bool DAESaver::skipFace(LLTextureEntry *te)
{
	return (gSavedSettings.getBOOL("DAEExportSkipTransparent")
		&& (te->getColor().mV[3] < 0.01f || te->getID() == DAEExportUtil::LL_TEXTURE_TRANSPARENT));
}

DAESaver::MaterialInfo DAESaver::getMaterial(LLTextureEntry* te)
{
	if (gSavedSettings.getBOOL("DAEExportConsolidateMaterials"))
	{
		for (const auto& mat : mAllMaterials)
		{
			if (mat.matches(te))
			{
				return mat;
			}
		}
	}

	MaterialInfo ret;
	ret.colorID = te->getID();
	const LLMaterialPtr materials = te->getMaterialParams();
	if (materials)
	{
		ret.normalID = materials->getNormalID();
		ret.specularID = materials->getSpecularID();
	}
	ret.color = te->getColor();
	ret.name = llformat("Material%d", mAllMaterials.size());
	mAllMaterials.push_back(ret);
	return ret;
}

void DAESaver::getMaterials(LLViewerObject* obj, material_list_t* ret)
{
	S32 num_faces = obj->getVolume()->getNumVolumeFaces();
	for (S32 face_num = 0; face_num < num_faces; ++face_num)
	{
		LLTextureEntry* te = obj->getTE(face_num);

		if (skipFace(te)) continue;

		MaterialInfo mat = getMaterial(te);

		if (!gSavedSettings.getBOOL("DAEExportConsolidateMaterials") || std::find(ret->begin(), ret->end(), mat) == ret->end())
		{
			ret->push_back(mat);
		}
	}
}

void DAESaver::getFacesWithMaterial(LLViewerObject* obj, const MaterialInfo& mat, int_list_t* ret)
{
	S32 num_faces = obj->getVolume()->getNumVolumeFaces();
	for (S32 face_num = 0; face_num < num_faces; ++face_num)
	{
		if (mat == getMaterial(obj->getTE(face_num)))
		{
			ret->push_back(face_num);
		}
	}
}

void DAESaver::generateEffects(daeElement *effects)
{
	// TODO: COLLADA for normal (bump) and specular maps on materials (see FCOLLADA extensions details)
	// TODO: Diffuse + color tint (see FCOLLADA extensions details)
	// Effects (face color, alpha)
	bool export_textures = gSavedSettings.getBOOL("DAEExportTextures");

	for (const auto& mat : mAllMaterials)
	{
		LLColor4 color = mat.color;
		domEffect* effect = (domEffect*)effects->add("effect");
		effect->setId((mat.name + "-fx").c_str());
		daeElement* profile = effect->add("profile_COMMON");
		std::string colladaName;

		if (export_textures)
		{
			LLUUID textID;
			U32 i = 0;
			for (; i < mTextures.size(); i++)
			{
				if (mat.colorID == mTextures[i])
				{
					textID = mTextures[i];
					break;
				}
			}

			if (!textID.isNull() && !mTextureNames[i].empty())
			{
				colladaName = mTextureNames[i] + "_" + mImageFormat;
				daeElement* newparam = profile->add("newparam");
				newparam->setAttribute("sid", (colladaName + "-surface").c_str());
				daeElement* surface = newparam->add("surface");
				surface->setAttribute("type", "2D");
				surface->add("init_from")->setCharData(colladaName.c_str());
				newparam = profile->add("newparam");
				newparam->setAttribute("sid", (colladaName + "-sampler").c_str());
				newparam->add("sampler2D source")->setCharData((colladaName + "-surface").c_str());
			}
		}

		daeElement* t = profile->add("technique");
		t->setAttribute("sid", "common");
		domElement* phong = t->add("phong");
		domElement* diffuse = phong->add("diffuse");
		// Only one <color> or <texture> can appear inside diffuse element
		if (!colladaName.empty())
		{
			daeElement* txtr = diffuse->add("texture");
			txtr->setAttribute("texture", (colladaName + "-sampler").c_str());
			txtr->setAttribute("texcoord", colladaName.c_str());
		}
		else
		{
			daeElement* diffuseColor = diffuse->add("color");
			diffuseColor->setAttribute("sid", "diffuse");
			diffuseColor->setCharData(llformat("%f %f %f %f", color.mV[0], color.mV[1], color.mV[2], color.mV[3]).c_str());
			phong->add("transparency float")->setCharData(llformat("%f", color.mV[3]).c_str());
		}
	}
}

void DAESaver::generateImagesSection(daeElement* images)
{
	for (const auto& name : mTextureNames)
	{
		if (name.empty()) continue;
		std::string colladaName = name + '_' + mImageFormat;
		daeElement* image = images->add("image");
		image->setAttribute("id", colladaName.c_str());
		image->setAttribute("name", colladaName.c_str());
		image->add("init_from")->setCharData(LLURI::escape(name + '.' + mImageFormat));
	}
}

class DAESaveSelectedObjects final : public view_listener_t
{
	bool handleEvent(LLPointer<LLOldEvents::LLEvent> event, const LLSD& userdata)
	{
		ColladaExportFloater* floater = new ColladaExportFloater();
		if (floater->addSelectedObjects())
		{
			floater->updateOverview();
			floater->open();
		}
		else
		{
			LLNotificationsUtil::add("ExportFailed");
			floater->close();
		}
		return true;
	}
};

class DAESaveSelectedAvatar : public view_listener_t
{
	bool handleEvent(LLPointer<LLOldEvents::LLEvent> event, const LLSD& userdata)
	{
		ColladaExportFloater* floater = new ColladaExportFloater();
		if (floater->addSelectedAvatar())
		{
			floater->updateOverview();
			floater->open();
		}
		else
		{
			LLNotificationsUtil::add("ExportFailed");
			floater->close();
		}
		return true;
	}
};


void addMenu(view_listener_t* menu, const std::string& name);
void add_dae_listeners() // Called in llviewermenu with other addMenu calls, function linked against
{
	addMenu(new DAESaveSelectedObjects(), "Object.SaveAsDAE");
	addMenu(new DAESaveSelectedAvatar(), "Avatar.SaveAsDAE");
}
