/*****************************************************************
|
|   Platinum - Miccro Media Controller
|
| Copyright (c) 2004-2010, Plutinosoft, LLC.
| All rights reserved.
| http://www.plutinosoft.com
|
| This program is free software; you can redistribute it and/or
| modify it under the terms of the GNU General Public License
| as published by the Free Software Foundation; either version 2
| of the License, or (at your option) any later version.
|
| OEMs, ISVs, VARs and other distributors that combine and 
| distribute commercially licensed software with Platinum software
| and do not wish to distribute the source code for the commercially
| licensed software under version 2, or (at your option) any later
| version, of the GNU General Public License (the "GPL") must enter
| into a commercial license agreement with Plutinosoft, LLC.
| 
| This program is distributed in the hope that it will be useful,
| but WITHOUT ANY WARRANTY; without even the implied warranty of
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
| GNU General Public License for more details.
|
| You should have received a copy of the GNU General Public License
| along with this program; see the file LICENSE.txt. If not, write to
| the Free Software Foundation, Inc., 
| 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
| http://www.gnu.org/licenses/gpl-2.0.html
|
****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "MediaFinder.h"
#include "PltLeaks.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

NPT_SET_LOCAL_LOGGER("platinum.tests.mediadfinder")

/*----------------------------------------------------------------------
|   Media_Finder::Media_Finder
+---------------------------------------------------------------------*/
Media_Finder::Media_Finder(PLT_CtrlPointReference& ctrlPoint, controllerInfo* cB) :
    PLT_SyncMediaBrowser(ctrlPoint),
    PLT_MediaController(ctrlPoint)
{
	cBaton = cB;
	NPT_LOG_INFO("Starting MicroMedia...");
	NPT_AutoLock lock(cBaton->m_ChangedLock);
	cBaton->hasChanged.SetValue(0);
	// create the stack that will be the directory where the
    // user is currently browsing. 
    // push the root directory onto the directory stack.
    m_CurBrowseDirectoryStack.Push("0");

    PLT_MediaController::SetDelegate(this);
}

/*----------------------------------------------------------------------
|   Media_Finder::Media_Finder
+---------------------------------------------------------------------*/
Media_Finder::~Media_Finder()
{
}


/*----------------------------------------------------------------------
|   Media_Finder::OnMRStateVariablesChanged
+---------------------------------------------------------------------*/
void 
Media_Finder::OnMRStateVariablesChanged(PLT_Service*  service, NPT_List<PLT_StateVariable*>*  vars){
		NPT_LOG_INFO("SERVICE TYPE ="+ service->GetServiceType());
		NPT_List<PLT_StateVariable*>::Iterator item = vars->GetFirstItem();
		NPT_String name, value;
		while (item) {
			name = (*item)->GetName();
			value = (*item)->GetValue();
			if(name == "Mute" || name == "Volume" || name == "TransportState"){
				EventInfo* newEvent = new EventInfo;
				newEvent->Name = name;
				newEvent->Value = value;
				newEvent->UUID = service->GetDevice()->GetUUID();
				NPT_AutoLock lockEvnt(cBaton->m_EventStack);
				cBaton->m_EventStack.Push(*newEvent);
				cBaton->hasChanged.SetValue(1);
			}
			item++;
		}
}


/*----------------------------------------------------------------------
|   Media_Finder::OnMSAdded
+---------------------------------------------------------------------*/
bool 
Media_Finder::OnMSAdded(PLT_DeviceDataReference& device) 
{   
	NPT_LOG_INFO(device->GetFriendlyName());
	NPT_LOG_INFO("Media Center Added");
    // Issue special action upon discovering MediaConnect server
    PLT_Service* service;
    if (NPT_SUCCEEDED(device->FindServiceByType("urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:*", service))) {
        PLT_ActionReference action;
        PLT_SyncMediaBrowser::m_CtrlPoint->CreateAction(
            device, 
            "urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:1", 
            "IsAuthorized", 
            action);
        if (!action.IsNull()) PLT_SyncMediaBrowser::m_CtrlPoint->InvokeAction(action, 0);

        PLT_SyncMediaBrowser::m_CtrlPoint->CreateAction(
            device, 
            "urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:1", 
            "IsValidated", 
            action);
        if (!action.IsNull()) PLT_SyncMediaBrowser::m_CtrlPoint->InvokeAction(action, 0);
    }

	//NPT_AutoLock lockChange(cBaton->m_ChangedLock);
	NPT_LOG_INFO("do some locking");
	NPT_LOG_INFO("Devices");
	NPT_AutoLock lockDevs(cBaton->m_DeviceStack);
	NPT_LOG_INFO("events");
	NPT_AutoLock lockEvnt(cBaton->m_EventStack);
	cBaton->m_DeviceStack.Push(device->GetUUID());

	EventInfo* newEvent = new EventInfo;
	newEvent->Name = "msAdd";
	newEvent->UUID = device->GetUUID();
	cBaton->m_EventStack.Push(*newEvent);
	cBaton->hasChanged.SetValue(1);

    return true; 
}

/*----------------------------------------------------------------------
|   Media_Finder::OnMRAdded
+---------------------------------------------------------------------*/
bool
Media_Finder::OnMRAdded(PLT_DeviceDataReference& device)
{
    NPT_String uuid = device->GetUUID();

    // test if it's a media renderer
    PLT_Service* service;
    if (NPT_SUCCEEDED(device->FindServiceByType("urn:schemas-upnp-org:service:AVTransport:*", service))) {
        NPT_AutoLock lock(m_MediaRenderers);
        m_MediaRenderers.Put(uuid, device);
				NPT_AutoLock lockEvnt(cBaton->m_EventStack);
				EventInfo* newEvent = new EventInfo;
				newEvent->Name = "mrAdd";
				newEvent->UUID = device->GetUUID();
				newEvent->Value = device->GetFriendlyName();
				cBaton->m_EventStack.Push(*newEvent);
				cBaton->hasChanged.SetValue(1);
    }
    
    return true;
}

/*----------------------------------------------------------------------
|   Media_Finder::OnMRRemoved
+---------------------------------------------------------------------*/
void
Media_Finder::OnMRRemoved(PLT_DeviceDataReference& device)
{
    NPT_String uuid = device->GetUUID();

    {
        NPT_AutoLock lock(m_MediaRenderers);
        m_MediaRenderers.Erase(uuid);
    }

    {
        NPT_AutoLock lock(m_CurMediaRendererLock);

        // if it's the currently selected one, we have to get rid of it
        if (!m_CurMediaRenderer.IsNull() && m_CurMediaRenderer == device) {
            m_CurMediaRenderer = NULL;
        }
    }
}

NPT_Result
Media_Finder::SetMR(NPT_String UUID){
	PLT_DeviceDataReference* result;
	NPT_AutoLock lock(m_CurMediaRendererLock);
	NPT_AutoLock MRLock(m_MediaRenderers);
	m_MediaRenderers.Get(UUID, result);
	if(!result)
		return NPT_FAILURE;
	else{
		m_CurMediaRenderer = *result;
		return NPT_SUCCESS;
	}
}

void
Media_Finder::GetCurMR(PLT_DeviceDataReference& renderer){
	NPT_AutoLock lock(m_CurMediaRendererLock);
	renderer = m_CurMediaRenderer;
}

PLT_DeviceMap Media_Finder::GetMRs(){
	PLT_DeviceMap res;
	NPT_AutoLock MRLock(m_MediaRenderers);
	res  = m_MediaRenderers;
	return res;

}

//results for all commands
void Media_Finder::OnCommandResult(NPT_Result               res, 
                                     PLT_DeviceDataReference& device,  
                                     void*                    userdata)
{
    NPT_COMPILER_UNUSED(device);
		NPT_LOG_INFO("results!!!!!");
    if (!userdata) return;
    PLT_BrowseData* data = (PLT_BrowseData*) userdata;
    (*data).res = res;
    (*data).shared_var.SetValue(1);
		NPT_LOG_INFO("set data");
}



/*
void
PLT_MicroMediaController::Play()
{
    PLT_DeviceDataReference device;
    GetCurMediaRenderer(device);
    if (!device.IsNull()) {
        Play(device, 0, "1", NULL);
    }
}*/

PLT_DeviceDataReference
Media_Finder::getDeviceReference(NPT_String UUID){

	NPT_AutoLock             lock(m_MediaServers);
	PLT_DeviceMap deviceList = GetMediaServersMap();
	PLT_DeviceDataReference* result = NULL;
	deviceList.Get(UUID, result);
	return result?*result:PLT_DeviceDataReference();
}

NPT_Result
Media_Finder::DoSearch(NPT_String UUID,NPT_String searchCriteria, PLT_MediaObjectListReference& resultList){
	NPT_Result res = NPT_FAILURE;
    PLT_DeviceDataReference device;
    device = getDeviceReference(UUID);
    if (!device.IsNull()) {
        NPT_String cur_object_id;

        // send off the browse packet and block 
		/*
        res = SearchSync(
            device, 
            "0", 
            resultList, 
            searchCriteria);		
    }*/

	res = BrowseSync(
            device, 
            "1$268435466", 
            resultList, 
            false,
		0);		
	}
	return res;

}
void
Media_Finder::Mute(bool value, PLT_BrowseData* status)
{
    PLT_DeviceDataReference device;
    GetCurMR(device);
    if (!device.IsNull()) {
        SetMute(device, 0, "Master", value, status);
    }else{
				status->res = -1;
				status->shared_var.SetValue(1);
		}
}

void
Media_Finder::Volume(int value, PLT_BrowseData* status)
{
    PLT_DeviceDataReference device;
    GetCurMR(device);
    if (!device.IsNull()) {
        SetVolume(device, 0, "Master", value, status);
    }else{
				status->res = -1;
				status->shared_var.SetValue(1);
		}
}


void
Media_Finder::PauseTrack(PLT_BrowseData* status)
{
    PLT_DeviceDataReference device;
    GetCurMR(device);
    if (!device.IsNull()) {
        Pause(device, 0, status);
    }else{
				status->res = -1;
				status->shared_var.SetValue(1);
		}
}

void
Media_Finder::StopTrack(PLT_BrowseData* status)
{
    PLT_DeviceDataReference device;
    GetCurMR(device);
    if (!device.IsNull()) {
        Stop(device, 0, status);
    }else{
				status->res = -1;
				status->shared_var.SetValue(1);
		}
}
/*
    void Media_Finder::OnGetMediaInfoResult(NPT_Result               res, 
	                             PLT_DeviceDataReference& device,
															 PLT_MediaInfo*						info,
	                             void*                    userdata)
	         {NPT_LOG_INFO("GOT MEDIA DATA");
						NPT_LOG_INFO(info->cur_uri);
						NPT_LOG_INFO(info->cur_metadata);
		
						};
*/
void
Media_Finder::PlayTrack(PLT_BrowseData* status)
{
    PLT_DeviceDataReference device;
    GetCurMR(device);
    if (!device.IsNull()) {
        Play(device, 0, "1", status);
    }else{
				status->res = -1;
				status->shared_var.SetValue(1);
		}
			//	GetMediaInfo(device, 0, NULL);
}

NPT_Result
Media_Finder::OpenNextTrack(NPT_Array<PLT_MediaItemResource> Resources,NPT_String Didl,PLT_BrowseData* status){
	NPT_Result res = NPT_FAILURE;
	PLT_DeviceDataReference device;
	
	GetCurMR(device);

	if(!device.IsNull()){
		if(Resources.GetItemCount() > 0) {
			// look for best resource to use by matching each resource to a sink advertised by renderer
			NPT_Cardinal resource_index = 0;
			PLT_MediaItem temp;
			temp.m_Resources = Resources;//FindBestResource wants a PLT_MediaItem, so make a temp one and give it our resource array
			if (NPT_FAILED(FindBestResource(device, temp, resource_index))) {
				NPT_LOG_SEVERE("track can't be played by media renderer");
				return res;
			}

			// invoke the setUri
			NPT_LOG_INFO("Issuing SetNextAVTransportURI");
			return SetNextAVTransportURI(device, 0, Resources[resource_index].m_Uri, Didl,(void*) status);
		//	return NPT_SUCCESS;
		} else {
			NPT_LOG_SEVERE("couldn't find resource");
			return res;
		}
	}
	NPT_LOG_SEVERE("no device selected");
	return res;
}



NPT_Result
Media_Finder::OpenTrack(NPT_Array<PLT_MediaItemResource> Resources,NPT_String Didl,PLT_BrowseData* status){
	NPT_Result res = NPT_FAILURE;
	PLT_DeviceDataReference device;
	
	GetCurMR(device);
	if(!device.IsNull()){
		if(Resources.GetItemCount() > 0) {
			// look for best resource to use by matching each resource to a sink advertised by renderer
			NPT_Cardinal resource_index = 0;
			PLT_MediaItem temp;
			temp.m_Resources = Resources;//FindBestResource wants a PLT_MediaItem, so make a temp one and give it our resource array
			if (NPT_FAILED(FindBestResource(device, temp, resource_index))) {
				NPT_LOG_SEVERE("track can't be played by media renderer");
				return res;
			}

			// invoke the setUri
			NPT_LOG_INFO("Issuing SetAVTransportURI");
			NPT_LOG_INFO_1("URI = %s",(char*)Resources[resource_index].m_Uri);
			SetAVTransportURI(device, 0, Resources[resource_index].m_Uri, Didl,(void*) status);
			return NPT_SUCCESS;
		} else {
			NPT_LOG_SEVERE("couldn't find resource");
			return res;
		}
	}
	NPT_LOG_SEVERE("no device selected");
	return res;
}

