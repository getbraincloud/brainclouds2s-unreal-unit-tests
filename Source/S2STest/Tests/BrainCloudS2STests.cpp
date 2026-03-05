// BrainCloudS2STests.cpp — C++ automation tests for the BrainCloudS2SPlugin.
//
// Credentials are read from Config/BrainCloudSettings.ini:
//   [Credentials]
//   AppId=...
//   ServerName=...
//   S2SKey=...
//   S2SUrl=...
//
// Run from command line:
//   UnrealEditor.exe "E:\UnrealProjects\bcS2SUnitTests 5.3\S2STest.uproject" ^
//     -ExecCmds="Automation RunTests BrainCloudS2S" ^
//     -unattended -nopause -nosplash -nullrhi -log

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "BrainCloudS2S.h"
#include "S2SRTTComms.h"
#include "S2SGlobalFileV3.h"
#include "S2SServiceName.h"
#include "S2SServiceOperation.h"
#include "S2SOperationParam.h"

// ============================================================
// Credential loading from Config/BrainCloudSettings.ini
// ============================================================
struct FS2SCredentials
{
	FString AppId;
	FString ServerName;
	FString ServerSecret;  // read from S2SKey
	FString Url;           // read from S2SUrl
};

static FS2SCredentials LoadCredentials()
{
	FS2SCredentials Creds;
	const FString IniPath = FPaths::ProjectConfigDir() / TEXT("BrainCloudSettings.ini");

	// Read the file directly rather than going through GConfig, which is
	// unreliable for custom ini files outside the standard UE config hierarchy.
	FConfigFile ConfigFile;
	ConfigFile.Read(IniPath);

	ConfigFile.GetString(TEXT("Credentials"), TEXT("AppId"),      Creds.AppId);
	ConfigFile.GetString(TEXT("Credentials"), TEXT("ServerName"), Creds.ServerName);
	ConfigFile.GetString(TEXT("Credentials"), TEXT("S2SKey"),     Creds.ServerSecret);
	ConfigFile.GetString(TEXT("Credentials"), TEXT("S2SUrl"),     Creds.Url);

	return Creds;
}

// ============================================================
// Shared test state
// ============================================================
struct FS2STestState
{
	UBrainCloudS2S* S2S = nullptr;
	bool bDone          = false;
	bool bSuccess       = false;
	FString Result;
};

// ============================================================
// Latent command: pumps runCallbacks() each frame until done or timed out
// ============================================================
class FWaitForS2SResponseCommand : public IAutomationLatentCommand
{
public:
	FWaitForS2SResponseCommand(TSharedRef<FS2STestState> InState, float InTimeoutSeconds = 15.0f)
		: State(InState)
		, TimeoutSeconds(InTimeoutSeconds)
		, StartTime(FPlatformTime::Seconds())
	{}

	virtual bool Update() override
	{
		if (State->S2S)
		{
			State->S2S->runCallbacks();
		}

		if (State->bDone)
		{
			return true;
		}

		if ((FPlatformTime::Seconds() - StartTime) >= static_cast<double>(TimeoutSeconds))
		{
			State->Result = TEXT("Timed out waiting for S2S response");
			State->bDone  = true;
			return true;
		}

		return false;
	}

private:
	TSharedRef<FS2STestState> State;
	float  TimeoutSeconds;
	double StartTime;
};

// ============================================================
// Helpers
// ============================================================

/** Build an S2S message JSON with no data object. */
static FString BuildS2SRequest(const TCHAR* Service, const TCHAR* Operation)
{
	TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("service"),   Service);
	Request->SetStringField(TEXT("operation"), Operation);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Request, Writer);
	return Out;
}

/** Build an S2S message JSON with a pre-populated data object. */
static FString BuildS2SRequest(const TCHAR* Service, const TCHAR* Operation,
                               const TSharedRef<FJsonObject>& Data)
{
	TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("service"),   Service);
	Request->SetStringField(TEXT("operation"), Operation);
	Request->SetObjectField(TEXT("data"),      Data);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Request, Writer);
	return Out;
}

static bool ParseS2SSuccess(const FString& JsonResult)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonResult);
	if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
	{
		const int32 Status = Json->HasField(TEXT("status"))
			? static_cast<int32>(Json->GetNumberField(TEXT("status")))
			: 0;
		return Status == 200;
	}
	return false;
}

static UBrainCloudS2S* MakeTestContext(const FS2SCredentials& Creds)
{
	UBrainCloudS2S* S2S = UBrainCloudS2S::CreateS2SContext(
		Creds.AppId, Creds.ServerName, Creds.ServerSecret, Creds.Url, false);
	S2S->AddToRoot();
	S2S->setLogEnabled(true);
	return S2S;
}

// ============================================================
// BrainCloudS2S.Authenticate
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrainCloudAuthenticateTest, "BrainCloudS2S.Authenticate",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBrainCloudAuthenticateTest::RunTest(const FString& Parameters)
{
	TSharedRef<FS2STestState> State = MakeShared<FS2STestState>();
	State->S2S = MakeTestContext(LoadCredentials());

	State->S2S->authenticate([State](const FString& Result)
	{
		State->Result   = Result;
		State->bSuccess = ParseS2SSuccess(Result);
		State->bDone    = true;
	});

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForS2SResponseCommand(State));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]() -> bool
	{
		TestTrue(TEXT("Authentication succeeded"), State->bSuccess);
		State->S2S->RemoveFromRoot();
		return true;
	}));

	return true;
}

// ============================================================
// BrainCloudS2S.Request  (authenticate → send a raw S2S request)
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrainCloudRequestTest, "BrainCloudS2S.Request",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBrainCloudRequestTest::RunTest(const FString& Parameters)
{
	TSharedRef<FS2STestState> State = MakeShared<FS2STestState>();
	State->S2S = MakeTestContext(LoadCredentials());

	// Chain: authenticate → request
	State->S2S->authenticate([State](const FString& AuthResult)
	{
		if (!ParseS2SSuccess(AuthResult))
		{
			State->Result = AuthResult;
			State->bDone  = true;
			return;
		}

		TSharedRef<FJsonObject> ListData = MakeShared<FJsonObject>();
		ListData->SetStringField(S2SOperationParam::FolderPath, TEXT(""));
		ListData->SetBoolField(S2SOperationParam::Recurse, true);
		const FString RequestJson = BuildS2SRequest(
			S2SServiceName::GlobalFileV3, S2SServiceOperation::SysGetGlobalFileList, ListData);

		State->S2S->request(RequestJson, [State](const FString& Result)
		{
			State->Result   = Result;
			State->bSuccess = ParseS2SSuccess(Result);
			State->bDone    = true;
		});
	});

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForS2SResponseCommand(State));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]() -> bool
	{
		TestTrue(TEXT("S2S request succeeded"), State->bSuccess);
		State->S2S->RemoveFromRoot();
		return true;
	}));

	return true;
}

// ============================================================
// BrainCloudS2S.RTT.Enable
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrainCloudRTTEnableTest, "BrainCloudS2S.RTT.Enable",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBrainCloudRTTEnableTest::RunTest(const FString& Parameters)
{
	TSharedRef<FS2STestState> State = MakeShared<FS2STestState>();
	State->S2S = MakeTestContext(LoadCredentials());

	State->S2S->authenticate([State](const FString& AuthResult)
	{
		if (!ParseS2SSuccess(AuthResult))
		{
			State->Result = AuthResult;
			State->bDone  = true;
			return;
		}

		State->S2S->GetRTTComms()->enableRTT(
			[State](const FString& Result)
			{
				State->Result   = Result;
				State->bSuccess = true;
				State->bDone    = true;
			},
			[State](const FString& Result)
			{
				State->Result   = Result;
				State->bSuccess = false;
				State->bDone    = true;
			});
	});

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForS2SResponseCommand(State, 30.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]() -> bool
	{
		TestTrue(TEXT("RTT enabled successfully"), State->bSuccess);
		State->S2S->RemoveFromRoot();
		return true;
	}));

	return true;
}

// ============================================================
// BrainCloudS2S.RTT.Disable  (enable RTT, then disable it)
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrainCloudRTTDisableTest, "BrainCloudS2S.RTT.Disable",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBrainCloudRTTDisableTest::RunTest(const FString& Parameters)
{
	TSharedRef<FS2STestState> State = MakeShared<FS2STestState>();
	State->S2S = MakeTestContext(LoadCredentials());

	State->S2S->authenticate([State](const FString& AuthResult)
	{
		if (!ParseS2SSuccess(AuthResult))
		{
			State->Result = AuthResult;
			State->bDone  = true;
			return;
		}

		State->S2S->GetRTTComms()->enableRTT(
			[State](const FString& /* Result */)
			{
				State->S2S->GetRTTComms()->disableRTT();
				State->bSuccess = true;
				State->bDone    = true;
			},
			[State](const FString& Result)
			{
				State->Result   = Result;
				State->bSuccess = false;
				State->bDone    = true;
			});
	});

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForS2SResponseCommand(State, 30.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]() -> bool
	{
		TestTrue(TEXT("RTT disabled successfully"), State->bSuccess);
		State->S2S->RemoveFromRoot();
		return true;
	}));

	return true;
}

// ============================================================
// BrainCloudS2S.RTT.SendMessage  (auth → enable RTT → join sys channel → send chat)
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrainCloudRTTSendMessageTest, "BrainCloudS2S.RTT.SendMessage",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBrainCloudRTTSendMessageTest::RunTest(const FString& Parameters)
{
	const FS2SCredentials Creds = LoadCredentials();

	TSharedRef<FS2STestState> State = MakeShared<FS2STestState>();
	State->S2S = MakeTestContext(Creds);

	const FString ChannelId = Creds.AppId + TEXT(":sy:mysyschannel");

	State->S2S->authenticate([State, ChannelId](const FString& AuthResult)
	{
		if (!ParseS2SSuccess(AuthResult))
		{
			State->Result = AuthResult;
			State->bDone  = true;
			return;
		}

		State->S2S->GetRTTComms()->enableRTT(
			[State, ChannelId](const FString& /* RTTResult */)
			{
				// Join the sys channel
				TSharedRef<FJsonObject> JoinData = MakeShared<FJsonObject>();
				JoinData->SetStringField(S2SOperationParam::ChannelId, ChannelId);
				JoinData->SetNumberField(S2SOperationParam::MaxReturn, 0);
				const FString JoinJson = BuildS2SRequest(
					S2SServiceName::Chat, S2SServiceOperation::SysChannelConnect, JoinData);

				State->S2S->request(JoinJson, [State, ChannelId](const FString& JoinResult)
				{
					if (!ParseS2SSuccess(JoinResult))
					{
						State->Result = JoinResult;
						State->bDone  = true;
						return;
					}

					// Send a chat message
					TSharedRef<FJsonObject> ContentData = MakeShared<FJsonObject>();
					ContentData->SetStringField(TEXT("foo"),      TEXT("bar"));
					ContentData->SetStringField(TEXT("someData"), TEXT("12345"));

					TSharedRef<FJsonObject> MsgData = MakeShared<FJsonObject>();
					MsgData->SetStringField(S2SOperationParam::ChannelId,       ChannelId);
					MsgData->SetObjectField(S2SOperationParam::Content,         ContentData);
					MsgData->SetBoolField(S2SOperationParam::RecordInHistory,   false);
					const FString MsgJson = BuildS2SRequest(
						S2SServiceName::Chat, S2SServiceOperation::SysPostChatMessage, MsgData);

					State->S2S->request(MsgJson, [State](const FString& MsgResult)
					{
						State->Result   = MsgResult;
						State->bSuccess = ParseS2SSuccess(MsgResult);
						State->bDone    = true;
					});
				});
			},
			[State](const FString& Result)
			{
				State->Result   = Result;
				State->bSuccess = false;
				State->bDone    = true;
			});
	});

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForS2SResponseCommand(State, 30.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]() -> bool
	{
		TestTrue(TEXT("RTT chat message sent successfully"), State->bSuccess);
		State->S2S->RemoveFromRoot();
		return true;
	}));

	return true;
}

// ============================================================
// BrainCloudS2S.GlobalFileV3.GetFileList
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrainCloudGFV3GetFileListTest, "BrainCloudS2S.GlobalFileV3.GetFileList",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBrainCloudGFV3GetFileListTest::RunTest(const FString& Parameters)
{
	TSharedRef<FS2STestState> State = MakeShared<FS2STestState>();
	State->S2S = MakeTestContext(LoadCredentials());

	State->S2S->authenticate([State](const FString& AuthResult)
	{
		if (!ParseS2SSuccess(AuthResult))
		{
			State->Result = AuthResult;
			State->bDone  = true;
			return;
		}

		State->S2S->GetGlobalFileV3()->sysGetGlobalFileList(TEXT(""), true,
			[State](const FString& Result)
			{
				State->Result   = Result;
				State->bSuccess = ParseS2SSuccess(Result);
				State->bDone    = true;
			});
	});

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForS2SResponseCommand(State));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]() -> bool
	{
		TestTrue(TEXT("GlobalFileV3 GetFileList succeeded"), State->bSuccess);
		State->S2S->RemoveFromRoot();
		return true;
	}));

	return true;
}

// ============================================================
// BrainCloudS2S.GlobalFileV3.CheckFilenameExists
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrainCloudGFV3CheckFilenameExistsTest, "BrainCloudS2S.GlobalFileV3.CheckFilenameExists",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBrainCloudGFV3CheckFilenameExistsTest::RunTest(const FString& Parameters)
{
	TSharedRef<FS2STestState> State = MakeShared<FS2STestState>();
	State->S2S = MakeTestContext(LoadCredentials());

	State->S2S->authenticate([State](const FString& AuthResult)
	{
		if (!ParseS2SSuccess(AuthResult))
		{
			State->Result = AuthResult;
			State->bDone  = true;
			return;
		}

		// Check for a file that likely doesn't exist — the call should still return status 200
		State->S2S->GetGlobalFileV3()->sysCheckFilenameExists(TEXT(""), TEXT("__automation_test_probe__.txt"),
			[State](const FString& Result)
			{
				State->Result   = Result;
				State->bSuccess = ParseS2SSuccess(Result);
				State->bDone    = true;
			});
	});

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForS2SResponseCommand(State));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]() -> bool
	{
		TestTrue(TEXT("GlobalFileV3 CheckFilenameExists succeeded"), State->bSuccess);
		State->S2S->RemoveFromRoot();
		return true;
	}));

	return true;
}

// ============================================================
// BrainCloudS2S.GlobalFileV3.CreateAndDeleteFolder
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrainCloudGFV3CreateDeleteFolderTest, "BrainCloudS2S.GlobalFileV3.CreateAndDeleteFolder",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBrainCloudGFV3CreateDeleteFolderTest::RunTest(const FString& Parameters)
{
	TSharedRef<FS2STestState> State = MakeShared<FS2STestState>();
	State->S2S = MakeTestContext(LoadCredentials());

	// Unique name per run so a previously orphaned folder never causes a collision
	const FString FolderName = TEXT("automation_test_") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);

	State->S2S->authenticate([State, FolderName](const FString& AuthResult)
	{
		if (!ParseS2SSuccess(AuthResult))
		{
			State->Result = AuthResult;
			State->bDone  = true;
			return;
		}

		// Create a temporary folder at the root (treeVersion=-1 skips version check)
		State->S2S->GetGlobalFileV3()->sysCreateFolder(
			TEXT(""), -1,
			FolderName, TEXT(""),
			false,
			[State, FolderName](const FString& CreateResult)
			{
				if (!ParseS2SSuccess(CreateResult))
				{
					State->Result = CreateResult;
					State->bDone  = true;
					return;
				}

				// Parse the new treeId from data.createdTreeId
				FString TreeId;
				TSharedPtr<FJsonObject> Json;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CreateResult);
				if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
				{
					const TSharedPtr<FJsonObject>* DataObj;
					if (Json->TryGetObjectField(TEXT("data"), DataObj))
					{
						(*DataObj)->TryGetStringField(TEXT("createdTreeId"), TreeId);
					}
				}

				if (TreeId.IsEmpty())
				{
					// Couldn't get treeId — report the create result as the outcome
					State->Result   = CreateResult;
					State->bSuccess = false;
					State->bDone    = true;
					return;
				}

				// Delete it immediately (treeVersion=-1, force=false since it's empty)
				State->S2S->GetGlobalFileV3()->sysDeleteFolder(
					TreeId, FolderName, -1, false,
					[State](const FString& DeleteResult)
					{
						State->Result   = DeleteResult;
						State->bSuccess = ParseS2SSuccess(DeleteResult);
						State->bDone    = true;
					});
			});
	});

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForS2SResponseCommand(State));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]() -> bool
	{
		TestTrue(TEXT("GlobalFileV3 CreateAndDeleteFolder succeeded"), State->bSuccess);
		State->S2S->RemoveFromRoot();
		return true;
	}));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
