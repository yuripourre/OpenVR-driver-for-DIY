//============ Copyright (c) Valve Corporation, All rights reserved. ============
//=============== Changed by r57zone (https://github.com/r57zone) ===============

#include <openvr_driver.h>
#include "DummyController.h" //Controllers implementation from https://github.com/terminal29/Simple-OpenVR-Driver-Tutorial

#include <vector>
#include <thread>
#include <chrono>

#if defined( _WINDOWS )
#include <Windows.h>
#endif

using namespace vr;


#if defined(_WIN32)
#define HMD_DLL_EXPORT extern "C" __declspec( dllexport )
#define HMD_DLL_IMPORT extern "C" __declspec( dllimport )
#elif defined(__GNUC__) || defined(COMPILER_GCC) || defined(__APPLE__)
#define HMD_DLL_EXPORT extern "C" __attribute__((visibility("default")))
#define HMD_DLL_IMPORT extern "C" 
#else
#error "Unsupported Platform."
#endif

inline HmdQuaternion_t HmdQuaternion_Init( double w, double x, double y, double z )
{
	HmdQuaternion_t quat;
	quat.w = w;
	quat.x = x;
	quat.y = y;
	quat.z = z;
	return quat;
}

inline void HmdMatrix_SetIdentity( HmdMatrix34_t *pMatrix )
{
	pMatrix->m[0][0] = 1.f;
	pMatrix->m[0][1] = 0.f;
	pMatrix->m[0][2] = 0.f;
	pMatrix->m[0][3] = 0.f;
	pMatrix->m[1][0] = 0.f;
	pMatrix->m[1][1] = 1.f;
	pMatrix->m[1][2] = 0.f;
	pMatrix->m[1][3] = 0.f;
	pMatrix->m[2][0] = 0.f;
	pMatrix->m[2][1] = 0.f;
	pMatrix->m[2][2] = 1.f;
	pMatrix->m[2][3] = 0.f;
}


// keys for use with the settings API
static const char * const k_pch_Sample_Section = "driver_null";
static const char * const k_pch_Sample_SerialNumber_String = "serialNumber";
static const char * const k_pch_Sample_ModelNumber_String = "modelNumber";
static const char * const k_pch_Sample_WindowX_Int32 = "windowX";
static const char * const k_pch_Sample_WindowY_Int32 = "windowY";
static const char * const k_pch_Sample_WindowWidth_Int32 = "windowWidth";
static const char * const k_pch_Sample_WindowHeight_Int32 = "windowHeight";
static const char * const k_pch_Sample_RenderWidth_Int32 = "renderWidth";
static const char * const k_pch_Sample_RenderHeight_Int32 = "renderHeight";
static const char * const k_pch_Sample_SecondsFromVsyncToPhotons_Float = "secondsFromVsyncToPhotons";
static const char * const k_pch_Sample_DisplayFrequency_Float = "displayFrequency";

//Head tracking vars
double yaw = 0, pitch = 0, roll = 0;
double pX = 0, pY = 0, pZ = 0;
double t0, t1, t2, t3, t4, t5;

DummyController controller_left;
DummyController controller_right;

double cyaw = 0, cpitch = 0, croll = 0;
double cpX = 0, cpY = 0, cpZ = 0;
double ct0, ct1, ct2, ct3, ct4, ct5;

double c2pX = 0, c2pY = 0, c2pZ = 0;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

class CWatchdogDriver_Sample : public IVRWatchdogProvider
{
public:
	CWatchdogDriver_Sample()
	{
		m_pWatchdogThread = nullptr;
	}

	virtual EVRInitError Init( vr::IVRDriverContext *pDriverContext ) ;

	virtual void Cleanup() ;

private:
	std::thread *m_pWatchdogThread;
};

CWatchdogDriver_Sample g_watchdogDriverNull;


bool g_bExiting = false;

void WatchdogThreadFunction(  )
{
	while ( !g_bExiting )
	{
#if defined( _WINDOWS )

		// on windows send the event when the Y key is pressed.
		//if ( (0x01 & GetAsyncKeyState( 'Y' )) != 0 )
		//{
			// Y key was pressed. 
			//vr::VRWatchdogHost()->WatchdogWakeUp();
		//}
		std::this_thread::sleep_for( std::chrono::microseconds( 500 ) );
#else
		// for the other platforms, just send one every five seconds
		std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
		vr::VRWatchdogHost()->WatchdogWakeUp();
#endif
	}
}

EVRInitError CWatchdogDriver_Sample::Init( vr::IVRDriverContext *pDriverContext )
{
	VR_INIT_WATCHDOG_DRIVER_CONTEXT( pDriverContext );
	//InitDriverLog( vr::VRDriverLog() );

	// Watchdog mode on Windows starts a thread that listens for the 'Y' key on the keyboard to 
	// be pressed. A real driver should wait for a system button event or something else from the 
	// the hardware that signals that the VR system should start up.
	g_bExiting = false;
	m_pWatchdogThread = new std::thread( WatchdogThreadFunction );
	if ( !m_pWatchdogThread )
	{
	//	DriverLog( "Unable to create watchdog thread\n");
		return VRInitError_Driver_Failed;
	}

	return VRInitError_None;
}


void CWatchdogDriver_Sample::Cleanup()
{
	g_bExiting = true;
	if ( m_pWatchdogThread )
	{
		m_pWatchdogThread->join();
		delete m_pWatchdogThread;
		m_pWatchdogThread = nullptr;
	}

	//CleanupDriverLog();
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
class CSampleDeviceDriver : public vr::ITrackedDeviceServerDriver, public vr::IVRDisplayComponent
{
public:
	CSampleDeviceDriver(  )
	{
		m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
		m_ulPropertyContainer = vr::k_ulInvalidPropertyContainer;

		//DriverLog( "Using settings values\n" );
		m_flIPD = vr::VRSettings()->GetFloat( k_pch_SteamVR_Section, k_pch_SteamVR_IPD_Float );

		char buf[1024];
		vr::VRSettings()->GetString( k_pch_Sample_Section, k_pch_Sample_SerialNumber_String, buf, sizeof( buf ) );
		m_sSerialNumber = buf;

		vr::VRSettings()->GetString( k_pch_Sample_Section, k_pch_Sample_ModelNumber_String, buf, sizeof( buf ) );
		m_sModelNumber = buf;

		m_nWindowX = vr::VRSettings()->GetInt32( k_pch_Sample_Section, k_pch_Sample_WindowX_Int32 );
		m_nWindowY = vr::VRSettings()->GetInt32( k_pch_Sample_Section, k_pch_Sample_WindowY_Int32 );
		m_nWindowWidth = vr::VRSettings()->GetInt32( k_pch_Sample_Section, k_pch_Sample_WindowWidth_Int32 );
		m_nWindowHeight = vr::VRSettings()->GetInt32( k_pch_Sample_Section, k_pch_Sample_WindowHeight_Int32 );
		m_nRenderWidth = vr::VRSettings()->GetInt32( k_pch_Sample_Section, k_pch_Sample_RenderWidth_Int32 );
		m_nRenderHeight = vr::VRSettings()->GetInt32( k_pch_Sample_Section, k_pch_Sample_RenderHeight_Int32 );
		m_flSecondsFromVsyncToPhotons = vr::VRSettings()->GetFloat( k_pch_Sample_Section, k_pch_Sample_SecondsFromVsyncToPhotons_Float );
		m_flDisplayFrequency = vr::VRSettings()->GetFloat( k_pch_Sample_Section, k_pch_Sample_DisplayFrequency_Float );

		//DriverLog( "driver_null: Serial Number: %s\n", m_sSerialNumber.c_str() );
		//DriverLog( "driver_null: Model Number: %s\n", m_sModelNumber.c_str() );
		//DriverLog( "driver_null: Window: %d %d %d %d\n", m_nWindowX, m_nWindowY, m_nWindowWidth, m_nWindowHeight );
		//DriverLog( "driver_null: Render Target: %d %d\n", m_nRenderWidth, m_nRenderHeight );
		//DriverLog( "driver_null: Seconds from Vsync to Photons: %f\n", m_flSecondsFromVsyncToPhotons );
		//DriverLog( "driver_null: Display Frequency: %f\n", m_flDisplayFrequency );
		//DriverLog( "driver_null: IPD: %f\n", m_flIPD );
	}

	virtual ~CSampleDeviceDriver()
	{
	}


	virtual EVRInitError Activate( vr::TrackedDeviceIndex_t unObjectId ) 
	{
		m_unObjectId = unObjectId;
		m_ulPropertyContainer = vr::VRProperties()->TrackedDeviceToPropertyContainer( m_unObjectId );

		vr::VRProperties()->SetStringProperty( m_ulPropertyContainer, Prop_ModelNumber_String, m_sModelNumber.c_str() );
		vr::VRProperties()->SetStringProperty( m_ulPropertyContainer, Prop_RenderModelName_String, m_sModelNumber.c_str() );
		vr::VRProperties()->SetFloatProperty( m_ulPropertyContainer, Prop_UserIpdMeters_Float, m_flIPD );
		vr::VRProperties()->SetFloatProperty( m_ulPropertyContainer, Prop_UserHeadToEyeDepthMeters_Float, 0.f );
		vr::VRProperties()->SetFloatProperty( m_ulPropertyContainer, Prop_DisplayFrequency_Float, m_flDisplayFrequency );
		vr::VRProperties()->SetFloatProperty( m_ulPropertyContainer, Prop_SecondsFromVsyncToPhotons_Float, m_flSecondsFromVsyncToPhotons );

		// return a constant that's not 0 (invalid) or 1 (reserved for Oculus)
		vr::VRProperties()->SetUint64Property( m_ulPropertyContainer, Prop_CurrentUniverseId_Uint64, 2 );

		// avoid "not fullscreen" warnings from vrmonitor
		vr::VRProperties()->SetBoolProperty( m_ulPropertyContainer, Prop_IsOnDesktop_Bool, false );

		//Debug mode activate Windowed Mode (borderless fullscreen) on "Headset Window" and you can move window to second screen with buttons (Shift + Win + Right or Left) 
		vr::VRProperties()->SetBoolProperty(m_ulPropertyContainer, Prop_DisplayDebugMode_Bool, true);

		// Icons can be configured in code or automatically configured by an external file "drivername\resources\driver.vrresources".
		// Icon properties NOT configured in code (post Activate) are then auto-configured by the optional presence of a driver's "drivername\resources\driver.vrresources".
		// In this manner a driver can configure their icons in a flexible data driven fashion by using an external file.
		//
		// The structure of the driver.vrresources file allows a driver to specialize their icons based on their HW.
		// Keys matching the value in "Prop_ModelNumber_String" are considered first, since the driver may have model specific icons.
		// An absence of a matching "Prop_ModelNumber_String" then considers the ETrackedDeviceClass ("HMD", "Controller", "GenericTracker", "TrackingReference")
		// since the driver may have specialized icons based on those device class names.
		//
		// An absence of either then falls back to the "system.vrresources" where generic device class icons are then supplied.
		//
		// Please refer to "bin\drivers\sample\resources\driver.vrresources" which contains this sample configuration.
		//
		// "Alias" is a reserved key and specifies chaining to another json block.
		//
		// In this sample configuration file (overly complex FOR EXAMPLE PURPOSES ONLY)....
		//
		// "Model-v2.0" chains through the alias to "Model-v1.0" which chains through the alias to "Model-v Defaults".
		//
		// Keys NOT found in "Model-v2.0" would then chase through the "Alias" to be resolved in "Model-v1.0" and either resolve their or continue through the alias.
		// Thus "Prop_NamedIconPathDeviceAlertLow_String" in each model's block represent a specialization specific for that "model".
		// Keys in "Model-v Defaults" are an example of mapping to the same states, and here all map to "Prop_NamedIconPathDeviceOff_String".
		//
		bool bSetupIconUsingExternalResourceFile = true;
		if ( !bSetupIconUsingExternalResourceFile )
		{
			// Setup properties directly in code.
			// Path values are of the form {drivername}\icons\some_icon_filename.png
			vr::VRProperties()->SetStringProperty( m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceOff_String, "{null}/icons/headset_sample_status_off.png" );
			vr::VRProperties()->SetStringProperty( m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceSearching_String, "{null}/icons/headset_sample_status_searching.gif" );
			vr::VRProperties()->SetStringProperty( m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceSearchingAlert_String, "{null}/icons/headset_sample_status_searching_alert.gif" );
			vr::VRProperties()->SetStringProperty( m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceReady_String, "{null}/icons/headset_sample_status_ready.png" );
			vr::VRProperties()->SetStringProperty( m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceReadyAlert_String, "{null}/icons/headset_sample_status_ready_alert.png" );
			vr::VRProperties()->SetStringProperty( m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceNotReady_String, "{null}/icons/headset_sample_status_error.png" );
			vr::VRProperties()->SetStringProperty( m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceStandby_String, "{null}/icons/headset_sample_status_standby.png" );
			vr::VRProperties()->SetStringProperty( m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceAlertLow_String, "{null}/icons/headset_sample_status_ready_low.png" );
		}

		return VRInitError_None;
	}

	virtual void Deactivate() 
	{
		m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
	}

	virtual void EnterStandby()
	{
	}

	void *GetComponent( const char *pchComponentNameAndVersion )
	{
		if ( !_stricmp( pchComponentNameAndVersion, vr::IVRDisplayComponent_Version ) )
		{
			return (vr::IVRDisplayComponent*)this;
		}

		// override this to add a component to a driver
		return NULL;
	}

	virtual void PowerOff() 
	{
	}

	/** debug request from a client */
	virtual void DebugRequest( const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize ) 
	{
		if( unResponseBufferSize >= 1 )
			pchResponseBuffer[0] = 0;
	}

	virtual void GetWindowBounds( int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) 
	{
		*pnX = m_nWindowX;
		*pnY = m_nWindowY;
		*pnWidth = m_nWindowWidth;
		*pnHeight = m_nWindowHeight;
	}

	virtual bool IsDisplayOnDesktop() 
	{
		return true;
	}

	virtual bool IsDisplayRealDisplay() 
	{
		return false;
	}

	virtual void GetRecommendedRenderTargetSize( uint32_t *pnWidth, uint32_t *pnHeight ) 
	{
		*pnWidth = m_nRenderWidth;
		*pnHeight = m_nRenderHeight;
	}

	virtual void GetEyeOutputViewport( EVREye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) 
	{
		*pnY = 0;
		*pnWidth = m_nWindowWidth / 2;
		*pnHeight = m_nWindowHeight;
	
		if ( eEye == Eye_Left )
		{
			*pnX = 0;
		}
		else
		{
			*pnX = m_nWindowWidth / 2;
		}
	}

	virtual void GetProjectionRaw( EVREye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom ) 
	{
		*pfLeft = -1.0;
		*pfRight = 1.0;
		*pfTop = -1.0;
		*pfBottom = 1.0;	
	}

	virtual DistortionCoordinates_t ComputeDistortion( EVREye eEye, float fU, float fV ) 
	{
		DistortionCoordinates_t coordinates;
		coordinates.rfBlue[0] = fU;
		coordinates.rfBlue[1] = fV;
		coordinates.rfGreen[0] = fU;
		coordinates.rfGreen[1] = fV;
		coordinates.rfRed[0] = fU;
		coordinates.rfRed[1] = fV;
		return coordinates;
	}

	virtual DriverPose_t GetPose()
	{
		DriverPose_t pose = { 0 };
		pose.poseIsValid = true;
		pose.result = TrackingResult_Running_OK;
		pose.deviceIsConnected = true;

		pose.qWorldFromDriverRotation = HmdQuaternion_Init(1, 0, 0, 0);
		pose.qDriverFromHeadRotation = HmdQuaternion_Init(1, 0, 0, 0);

		//Simple change yaw, pitch, roll with numpad keys
		if ((GetAsyncKeyState(VK_NUMPAD3) & 0x8000) != 0) yaw += 0.01;
		if ((GetAsyncKeyState(VK_NUMPAD1) & 0x8000) != 0) yaw += -0.01;

		if ((GetAsyncKeyState(VK_NUMPAD4) & 0x8000) != 0) pitch += 0.01;
		if ((GetAsyncKeyState(VK_NUMPAD6) & 0x8000) != 0) pitch += -0.01;

		if ((GetAsyncKeyState(VK_NUMPAD8) & 0x8000) != 0) roll += 0.01;
		if ((GetAsyncKeyState(VK_NUMPAD2) & 0x8000) != 0) roll += -0.01;

		if ((GetAsyncKeyState(VK_NUMPAD9) & 0x8000) != 0)
		{
			yaw = 0;
			pitch = 0;
			roll = 0;
		}

		if ((GetAsyncKeyState(VK_UP) & 0x8000) != 0) pZ += -0.01;
		if ((GetAsyncKeyState(VK_DOWN) & 0x8000) != 0) pZ += 0.01;
		
		if ((GetAsyncKeyState(VK_LEFT) & 0x8000) != 0) pX += -0.01;
		if ((GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0) pX += 0.01;
		
		if ((GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0) pY += 0.01;
		if ((GetAsyncKeyState(VK_NEXT) & 0x8000) != 0) pY += -0.01;

		if ((GetAsyncKeyState(VK_END) & 0x8000) != 0) { pX = 0; pY = 0; pZ = 0; }

		pose.vecPosition[0] = pX;
		pose.vecPosition[1] = pY;
		pose.vecPosition[2] = pZ;

		//Convert yaw, pitch, roll to quaternion
		t0 = cos(yaw * 0.5);
		t1 = sin(yaw * 0.5);
		t2 = cos(roll * 0.5);
		t3 = sin(roll * 0.5);
		t4 = cos(pitch * 0.5);
		t5 = sin(pitch * 0.5);

		//Set head tracking rotation
		pose.qRotation.w = t0 * t2 * t4 + t1 * t3 * t5;
		pose.qRotation.x = t0 * t3 * t4 - t1 * t2 * t5;
		pose.qRotation.y = t0 * t2 * t5 + t1 * t3 * t4;
		pose.qRotation.z = t1 * t2 * t4 - t0 * t3 * t5;

		return pose;
	}


	void RunFrame()
	{
		// In a real driver, this should happen from some pose tracking thread.
		// The RunFrame interval is unspecified and can be very irregular if some other
		// driver blocks it for some periodic task.
		if (m_unObjectId != vr::k_unTrackedDeviceIndexInvalid)
		{
			vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_unObjectId, GetPose(), sizeof(DriverPose_t));
		}



		//Controller1
		DriverPose_t left_pose = controller_left.GetPose();

		if ((GetAsyncKeyState(70) & 0x8000) != 0) cyaw += 0.1; //F
		if ((GetAsyncKeyState(72) & 0x8000) != 0) cyaw += -0.1; //H

		if ((GetAsyncKeyState(84) & 0x8000) != 0) croll += 0.1; //T
		if ((GetAsyncKeyState(71) & 0x8000) != 0) croll += -0.1; //G

		if ((GetAsyncKeyState(66) & 0x8000) != 0) //B
		{
			cpitch = 0;
			croll = 0;
		}

		//Change position controller1
		if ((GetAsyncKeyState(87) & 0x8000) != 0) cpZ += -0.01; //W
		if ((GetAsyncKeyState(83) & 0x8000) != 0) cpZ += 0.01; //S

		if ((GetAsyncKeyState(65) & 0x8000) != 0) cpX += -0.01; //A
		if ((GetAsyncKeyState(68) & 0x8000) != 0) cpX += 0.01; //D

		if ((GetAsyncKeyState(81) & 0x8000) != 0) cpY += 0.01; //Q
		if ((GetAsyncKeyState(69) & 0x8000) != 0) cpY += -0.01; //E

		if ((GetAsyncKeyState(82) & 0x8000) != 0) { cpX = 0; cpY = 0; cpZ = 0; } //R

		left_pose.vecPosition[0] = cpX;
		left_pose.vecPosition[1] = cpY;
		left_pose.vecPosition[2] = cpZ;

		//Convert yaw, pitch, roll to quaternion
		ct0 = cos(cyaw * 0.5);
		ct1 = sin(cyaw * 0.5);
		ct2 = cos(croll * 0.5);
		ct3 = sin(croll * 0.5);
		ct4 = cos(cpitch * 0.5);
		ct5 = sin(cpitch * 0.5);

		//Set head tracking rotation
		left_pose.qRotation.w = ct0 * ct2 * ct4 + ct1 * ct3 * ct5;
		left_pose.qRotation.x = ct0 * ct3 * ct4 - ct1 * ct2 * ct5;
		left_pose.qRotation.y = ct0 * ct2 * ct5 + ct1 * ct3 * ct4;
		left_pose.qRotation.z = ct1 * ct2 * ct4 - ct0 * ct3 * ct5;

		

		//controller2_state.ulButtonPressed = 0;
		//controller2_state.ulButtonTouched = 0;
		//controller2_state.rAxis[1].x = 0;
		//controller2_state.ulButtonPressed |= vr::ButtonMaskFromId(vr::k_EButton_System); 

		/* Buttons
					vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu) |
					vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad) |
					vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger) |
					vr::ButtonMaskFromId(vr::k_EButton_System) |
					vr::ButtonMaskFromId(vr::k_EButton_Grip);
		*/

		//Controller1
		VRControllerState_t controller1_state = controller_right.GetControllerState();

		if ((GetAsyncKeyState(90) & 0x8000) != 0) { //z
			VRServerDriverHost()->TrackedDeviceButtonPressed(controller_left.getObjectID(), vr::k_EButton_System, 0.0);
		}
		else {
			VRServerDriverHost()->TrackedDeviceButtonUnpressed(controller_left.getObjectID(), vr::k_EButton_System, 0.0);
		}

		if ((GetAsyncKeyState(88) & 0x8000) != 0) { //x
			VRServerDriverHost()->TrackedDeviceButtonPressed(controller_left.getObjectID(), vr::k_EButton_SteamVR_Trigger, 0.0);
			controller1_state.rAxis[1].x = 1.0f;
			VRServerDriverHost()->TrackedDeviceAxisUpdated(controller_left.getObjectID(), 1, controller1_state.rAxis[1]);
		}
		else {
			controller1_state.rAxis[1].x = 0.0f;
			VRServerDriverHost()->TrackedDeviceAxisUpdated(controller_left.getObjectID(), 1, controller1_state.rAxis[1]);
			VRServerDriverHost()->TrackedDeviceButtonUnpressed(controller_left.getObjectID(), vr::k_EButton_SteamVR_Trigger, 0.0);
		}


		if ((GetAsyncKeyState(67) & 0x8000) != 0) {//c
			VRServerDriverHost()->TrackedDeviceButtonPressed(controller_left.getObjectID(), vr::k_EButton_ApplicationMenu, 0.0);
		}
		else {
			VRServerDriverHost()->TrackedDeviceButtonUnpressed(controller_left.getObjectID(), vr::k_EButton_ApplicationMenu, 0.0);
		}

		if ((GetAsyncKeyState(86) & 0x8000) != 0) {//v
			VRServerDriverHost()->TrackedDeviceButtonPressed(controller_left.getObjectID(), vr::k_EButton_Grip, 0.0);
		}
		else {
			VRServerDriverHost()->TrackedDeviceButtonUnpressed(controller_left.getObjectID(), vr::k_EButton_Grip, 0.0);
		}

		if ((GetAsyncKeyState(49) & 0x8000) != 0) {//"1"
			VRServerDriverHost()->TrackedDeviceButtonPressed(controller_left.getObjectID(), vr::k_EButton_SteamVR_Touchpad, 0.0);
		}
		else {
			VRServerDriverHost()->TrackedDeviceButtonUnpressed(controller_left.getObjectID(), vr::k_EButton_SteamVR_Touchpad, 0.0);
		}


		//controller2_state.unPacketNum = controller2_state.unPacketNum + 1;

		//controller_left.updateControllerState(controller2_state);
		//controller_right.updateControllerState(controller2_state); //Set state controller2


		controller_left.updateControllerPose(left_pose);

		VRServerDriverHost()->TrackedDevicePoseUpdated(controller_left.getObjectID(), controller_left.GetPose(), sizeof(DriverPose_t));

		//Controller2

		VRControllerState_t controller2_state = controller_right.GetControllerState();

		if ((GetAsyncKeyState(78) & 0x8000) != 0) { //n
			VRServerDriverHost()->TrackedDeviceButtonPressed(controller_right.getObjectID(), vr::k_EButton_System, 0.0);
		}
		else {
			VRServerDriverHost()->TrackedDeviceButtonUnpressed(controller_right.getObjectID(), vr::k_EButton_System, 0.0);
		}

		if ((GetAsyncKeyState(188) & 0x8000) != 0) { //",<"
			VRServerDriverHost()->TrackedDeviceButtonPressed(controller_right.getObjectID(), vr::k_EButton_SteamVR_Trigger, 0.0);
			controller2_state.rAxis[1].x = 1.0f;
			VRServerDriverHost()->TrackedDeviceAxisUpdated(controller_right.getObjectID(), 1, controller2_state.rAxis[1]);
		}
		else {
			controller2_state.rAxis[1].x = 0.0f;
			VRServerDriverHost()->TrackedDeviceAxisUpdated(controller_right.getObjectID(), 1, controller2_state.rAxis[1]);
			VRServerDriverHost()->TrackedDeviceButtonUnpressed(controller_right.getObjectID(), vr::k_EButton_SteamVR_Trigger, 0.0);
		}


		if ((GetAsyncKeyState(190) & 0x8000) != 0) {//".>"
			VRServerDriverHost()->TrackedDeviceButtonPressed(controller_right.getObjectID(), vr::k_EButton_ApplicationMenu, 0.0);
		}
		else {
			VRServerDriverHost()->TrackedDeviceButtonUnpressed(controller_right.getObjectID(), vr::k_EButton_ApplicationMenu, 0.0);
		}

		if ((GetAsyncKeyState(191) & 0x8000) != 0) {//"/?"
			VRServerDriverHost()->TrackedDeviceButtonPressed(controller_right.getObjectID(), vr::k_EButton_Grip, 0.0);
		}
		else {
			VRServerDriverHost()->TrackedDeviceButtonUnpressed(controller_right.getObjectID(), vr::k_EButton_Grip, 0.0);
		}

		if ((GetAsyncKeyState(50) & 0x8000) != 0) {//"2"
			VRServerDriverHost()->TrackedDeviceButtonPressed(controller_right.getObjectID(), vr::k_EButton_SteamVR_Touchpad, 0.0);
		}
		else {
			VRServerDriverHost()->TrackedDeviceButtonUnpressed(controller_right.getObjectID(), vr::k_EButton_SteamVR_Touchpad, 0.0);
		}

		if ((GetAsyncKeyState(51) & 0x8000) != 0) { //"3" 
			controller2_state.rAxis[0].x = 1.0f;
			controller2_state.rAxis[0].y = 0.0f;
			VRServerDriverHost()->TrackedDeviceAxisUpdated(controller_right.getObjectID(), 0, controller2_state.rAxis[0]); 
		} 
		else {
			controller2_state.rAxis[0].x = 0.0f;
			controller2_state.rAxis[0].y = 0.0f;
			VRServerDriverHost()->TrackedDeviceAxisUpdated(controller_right.getObjectID(), 0, controller2_state.rAxis[0]);
		}

		DriverPose_t right_pose = controller_right.GetPose();

		if((GetAsyncKeyState(73) & 0x8000) != 0) c2pZ += -0.01; //I
		if ((GetAsyncKeyState(75) & 0x8000) != 0) c2pZ += 0.01; //K

		if ((GetAsyncKeyState(74) & 0x8000) != 0) c2pX += -0.01; //J
		if ((GetAsyncKeyState(76) & 0x8000) != 0) c2pX += 0.01; //L

		if ((GetAsyncKeyState(85) & 0x8000) != 0) c2pY += 0.01; //U
		if ((GetAsyncKeyState(79) & 0x8000) != 0) c2pY += -0.01; //O

		if ((GetAsyncKeyState(80) & 0x8000) != 0) { c2pX = 0; c2pY = 0; c2pZ = 0; } //P

		right_pose.vecPosition[0] = c2pX;
		right_pose.vecPosition[1] = c2pY;
		right_pose.vecPosition[2] = c2pZ;

		//Controllers rotation one for two 
		right_pose.qRotation.w = left_pose.qRotation.w;
		right_pose.qRotation.x = left_pose.qRotation.x;
		right_pose.qRotation.y = left_pose.qRotation.y;
		right_pose.qRotation.z = left_pose.qRotation.z;

		controller_right.updateControllerPose(right_pose);
		VRServerDriverHost()->TrackedDevicePoseUpdated(controller_right.getObjectID(), controller_right.GetPose(), sizeof(DriverPose_t));
	}

	std::string GetSerialNumber() const { return m_sSerialNumber; }

private:
	vr::TrackedDeviceIndex_t m_unObjectId;
	vr::PropertyContainerHandle_t m_ulPropertyContainer;

	std::string m_sSerialNumber;
	std::string m_sModelNumber;

	int32_t m_nWindowX;
	int32_t m_nWindowY;
	int32_t m_nWindowWidth;
	int32_t m_nWindowHeight;
	int32_t m_nRenderWidth;
	int32_t m_nRenderHeight;
	float m_flSecondsFromVsyncToPhotons;
	float m_flDisplayFrequency;
	float m_flIPD;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
class CServerDriver_Sample: public IServerTrackedDeviceProvider
{
public:
	CServerDriver_Sample()
		: m_pNullHmdLatest( NULL )
		, m_bEnableNullDriver( false )
	{
	}

	virtual EVRInitError Init( vr::IVRDriverContext *pDriverContext ) ;
	virtual void Cleanup() ;
	virtual const char * const *GetInterfaceVersions() { return vr::k_InterfaceVersions; }
	virtual void RunFrame() ;
	virtual bool ShouldBlockStandbyMode()  { return false; }
	virtual void EnterStandby()  {}
	virtual void LeaveStandby()  {}

private:
	CSampleDeviceDriver *m_pNullHmdLatest;
	
	bool m_bEnableNullDriver;
};

CServerDriver_Sample g_serverDriverNull;


EVRInitError CServerDriver_Sample::Init( vr::IVRDriverContext *pDriverContext )
{
	VR_INIT_SERVER_DRIVER_CONTEXT( pDriverContext );
	//InitDriverLog( vr::VRDriverLog() );

	m_pNullHmdLatest = new CSampleDeviceDriver();
	vr::VRServerDriverHost()->TrackedDeviceAdded( m_pNullHmdLatest->GetSerialNumber().c_str(), vr::TrackedDeviceClass_HMD, m_pNullHmdLatest );

	DriverPose_t test_pose = { 0 };
	test_pose.deviceIsConnected = true;
	test_pose.poseIsValid = true;
	test_pose.willDriftInYaw = false;
	test_pose.shouldApplyHeadModel = false;
	test_pose.poseTimeOffset = 0;
	test_pose.result = ETrackingResult::TrackingResult_Running_OK;
	test_pose.qDriverFromHeadRotation = { 1,0,0,0 };
	test_pose.qWorldFromDriverRotation = { 1,0,0,0 };

	VRControllerState_t test_state;
	test_state.ulButtonPressed = test_state.ulButtonTouched = 0;

	controller_left = DummyController("example_con1", false, test_pose, test_state);
	controller_right = DummyController("example_con2", true, test_pose, test_state);

	VRServerDriverHost()->TrackedDeviceAdded("example_con1", vr::TrackedDeviceClass_Controller, &controller_left);
	VRServerDriverHost()->TrackedDeviceAdded("example_con2", vr::TrackedDeviceClass_Controller, &controller_right);

	return VRInitError_None;
}

void CServerDriver_Sample::Cleanup() 
{
	//CleanupDriverLog();
	delete m_pNullHmdLatest;
	m_pNullHmdLatest = NULL;
}


void CServerDriver_Sample::RunFrame()
{
	if ( m_pNullHmdLatest )
	{
		m_pNullHmdLatest->RunFrame();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
HMD_DLL_EXPORT void *HmdDriverFactory( const char *pInterfaceName, int *pReturnCode )
{
	if( 0 == strcmp( IServerTrackedDeviceProvider_Version, pInterfaceName ) )
	{
		return &g_serverDriverNull;
	}
	if( 0 == strcmp( IVRWatchdogProvider_Version, pInterfaceName ) )
	{
		return &g_watchdogDriverNull;
	}

	if( pReturnCode )
		*pReturnCode = VRInitError_Init_InterfaceNotFound;

	return NULL;
}