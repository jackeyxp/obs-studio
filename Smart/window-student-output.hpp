
#pragma once

#include <obs.hpp>
#include <string>
#include <memory>

using namespace std;

class CStudentWindow;
class CStudentOutput
{
public:
	CStudentOutput(CStudentWindow * main_);
	~CStudentOutput();
public:
	bool StartStreaming();
	void StopStreaming();
	bool StartRecording();
	void StopRecording();
private:
	int GetAudioBitrate(size_t i) const; 
	void UpdateStreamSettings();
	void UpdateAudioSettings();
	void SetupStreaming();
	void SetupRecording();
	void SetupOutputs();
public:
	bool Active() const;
	bool StreamingActive() const;
	bool RecordingActive() const;
public:
	bool streamingActive = false;
	bool recordingActive = false;
	CStudentWindow * m_lpStudentMain = nullptr;
private:
	bool usesBitrate = false;
	bool useStreamEncoder = true;

	OBSOutput fileOutput;
	OBSOutput streamOutput;

	std::string strAudioEncID;
	OBSEncoder streamAudioEnc;
	OBSEncoder h264Streaming;

	OBSSignal startRecording;
	OBSSignal stopRecording;

	OBSSignal stopStreaming;
	OBSSignal startStreaming;
	OBSSignal statusStreaming;
};
