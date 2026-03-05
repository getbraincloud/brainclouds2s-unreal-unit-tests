// Copyright 2026 bitHeads, Inc. All Rights Reserved.

#include "S2SBlueprintTestLibrary.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogS2SBPTest, Log, All);

FS2STestCredentials US2SBlueprintTestLibrary::LoadS2STestCredentials()
{
	FS2STestCredentials Creds;
	const FString IniPath = FPaths::ProjectConfigDir() / TEXT("BrainCloudSettings.ini");

	FConfigFile ConfigFile;
	ConfigFile.Read(IniPath);

	ConfigFile.GetString(TEXT("Credentials"), TEXT("AppId"),      Creds.AppId);
	ConfigFile.GetString(TEXT("Credentials"), TEXT("ServerName"), Creds.ServerName);
	ConfigFile.GetString(TEXT("Credentials"), TEXT("S2SKey"),     Creds.ServerSecret);
	ConfigFile.GetString(TEXT("Credentials"), TEXT("S2SUrl"),     Creds.Url);

	return Creds;
}

bool US2SBlueprintTestLibrary::ReportResult(const FString& JsonResponse, const FString& Context)
{
	const FString Label = Context.IsEmpty() ? TEXT("S2S") : Context;

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonResponse);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		UE_LOG(LogS2SBPTest, Error, TEXT("[%s] Failed to parse response JSON: %s"), *Label, *JsonResponse);
		return false;
	}

	const int32 Status = Json->HasField(TEXT("status"))
		? static_cast<int32>(Json->GetNumberField(TEXT("status")))
		: 0;

	if (Status == 200)
	{
		UE_LOG(LogS2SBPTest, Log, TEXT("[%s] Success (status 200)"), *Label);
		return true;
	}

	const FString StatusMessage = Json->HasField(TEXT("status_message"))
		? Json->GetStringField(TEXT("status_message"))
		: TEXT("(no status_message)");

	const int32 ReasonCode = Json->HasField(TEXT("reason_code"))
		? static_cast<int32>(Json->GetNumberField(TEXT("reason_code")))
		: 0;

	UE_LOG(LogS2SBPTest, Error, TEXT("[%s] Failed — status: %d, reason_code: %d, message: %s"),
		*Label, Status, ReasonCode, *StatusMessage);

	return false;
}

bool US2SBlueprintTestLibrary::ReportResultSimple(bool success, const FString& message, const FString& Context)
{
	const FString Label = Context.IsEmpty() ? TEXT("S2S") : Context;

	if (success) {
		UE_LOG(LogS2SBPTest, Log, TEXT("[%s] Success - %s"), *Label, *message);
	}
	else {
		UE_LOG(LogS2SBPTest, Error, TEXT("[%s] Failed — %s"),
			*Label, *message);
	}

	return success;
}
