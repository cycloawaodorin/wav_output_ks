#include <Windows.h>
#include <mmreg.h>
#include <fstream>
#include "output2.hpp"
#include "config2.hpp"
#include "resource.hpp"
#include "version.hpp"

static bool func_output(OUTPUT_INFO *oip);
static bool func_config(HWND hwnd, HINSTANCE hinst);
static bool func_load_project_config(PROJECT_FILE* project);
static bool func_save_project_config(PROJECT_FILE* project);
static void load_config();
static void save_config();
static LPCWSTR func_get_config_text();

static struct {
	WORD format;
	WORD n_ch;
} config = {WAVE_FORMAT_PCM, 0u};
static const std::wstring auo_filename = L"wav_output_ks.auo2";
static const std::wstring config_filename = L"wav_output_ks.config";
static std::wstring config_path;

#define PLUGIN_NAME L"WAVファイル出力"

EXTERN_C OUTPUT_PLUGIN_TABLE *
GetOutputPluginTable()
{
	static OUTPUT_PLUGIN_TABLE opt = {
		OUTPUT_PLUGIN_TABLE::FLAG_AUDIO,
		PLUGIN_NAME,
		L"Wav File (*.wav)\0*.wav\0",
		PLUGIN_NAME L" " VERSION L" by KAZOON",
		func_output,
		func_config,
		func_get_config_text,
		func_load_project_config,
		func_save_project_config,
	};
	return &opt;
}

static DWORD
build_header(OUTPUT_INFO *oip, WAVEFORMATEX &wf)
{
	wf.wFormatTag = config.format;
	if ( config.n_ch == 0 ) {
		wf.nChannels = static_cast<WORD>(oip->audio_ch);
	} else {
		wf.nChannels = config.n_ch;
	}
	wf.nSamplesPerSec = static_cast<DWORD>(oip->audio_rate);
	if ( config.format == WAVE_FORMAT_IEEE_FLOAT ) {
		wf.wBitsPerSample = 32u;
	} else if ( config.format == WAVE_FORMAT_PCM ) {
		wf.wBitsPerSample = 16u;
	} else {
		return false;
	}
	wf.nBlockAlign = wf.nChannels * ( wf.wBitsPerSample / 8u );
	wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
	return wf.nBlockAlign * static_cast<DWORD>(oip->audio_n) + 36u;
}

static void
write_header(WAVEFORMATEX &wf, std::ofstream &ofs, DWORD &datasize)
{
	constexpr static const DWORD fccRIFF = mmioFOURCC('R', 'I', 'F', 'F');
	constexpr static const DWORD fccWAVE = mmioFOURCC('W', 'A', 'V', 'E');
	constexpr static const DWORD fccfmt = mmioFOURCC('f', 'm', 't', ' ');
	constexpr static const DWORD fccdata = mmioFOURCC('d', 'a', 't', 'a');
	
	ofs.write(reinterpret_cast<const char *>(&fccRIFF), sizeof(DWORD));
	ofs.write(reinterpret_cast<const char *>(&datasize), sizeof(DWORD));
	ofs.write(reinterpret_cast<const char *>(&fccWAVE), sizeof(DWORD));
	
	ofs.write(reinterpret_cast<const char *>(&fccfmt), sizeof(DWORD));
	constexpr static const DWORD wfsize = 16u;
	ofs.write(reinterpret_cast<const char *>(&wfsize), sizeof(DWORD));
	ofs.write(reinterpret_cast<const char *>(&wf), wfsize);
	
	datasize -= 36;
	ofs.write(reinterpret_cast<const char *>(&fccdata), sizeof(DWORD));
	ofs.write(reinterpret_cast<const char *>(&datasize), sizeof(DWORD));
}

static bool
write_asis(OUTPUT_INFO *oip, WAVEFORMATEX &wf, std::ofstream &ofs, int len)
{
	int readed = 0;
	for (auto i=0; i<oip->audio_n; i+=readed) {
		if ( oip->func_is_abort() ) { break; }
		oip->func_rest_time_disp(i, oip->audio_n);
		const char *data = static_cast<const char *>( oip->func_get_audio(i, len, &readed, config.format) );
		if ( readed == 0 ) { break; }
		ofs.write(data, readed*wf.nBlockAlign);
	}
	return true;
}

static bool
write_merge_f(OUTPUT_INFO *oip, WAVEFORMATEX &wf, std::ofstream &ofs, int len)
{
	int readed = 0;
	for (auto i=0; i<oip->audio_n; i+=readed) {
		if ( oip->func_is_abort() ) { break; }
		oip->func_rest_time_disp(i, oip->audio_n);
		const float *org = static_cast<const float *>( oip->func_get_audio(i, len, &readed, config.format) );
		if ( readed == 0 ) { break; }
		for (auto j=0; j<readed; j++) {
			float f = 0.0f;
			for (auto k=0; k<(oip->audio_ch); k++) {
				f += org[j*(oip->audio_ch)+k];
			}
			f /= static_cast<float>(oip->audio_ch);
			ofs.write(reinterpret_cast<const char *>(&f), wf.nBlockAlign);
		}
	}
	return true;
}

static std::int16_t
round2(int sum, int n)
{
	auto r = sum % n;
	if ( r*2 < n ) {
		return static_cast<std::int16_t>((sum-r)/n);
	} else if ( r*2 == n ) {
		r = (sum-r)/n;
		if ( (r&1) == 0 ) {
			return static_cast<std::int16_t>(r);
		} else {
			return static_cast<std::int16_t>(r+1);
		}
	} else {
		return static_cast<std::int16_t>((sum-r)/n+1);
	}
}

static bool
write_merge_s(OUTPUT_INFO *oip, WAVEFORMATEX &wf, std::ofstream &ofs, int len)
{
	int readed = 0;
	for (auto i=0; i<oip->audio_n; i+=readed) {
		if ( oip->func_is_abort() ) { break; }
		oip->func_rest_time_disp(i, oip->audio_n);
		const auto org = static_cast<const std::int16_t *>( oip->func_get_audio(i, len, &readed, config.format) );
		if ( readed == 0 ) { break; }
		for (auto j=0; j<readed; j++) {
			int s = 0;
			for (auto k=0; k<(oip->audio_ch); k++) {
				s += org[j*(oip->audio_ch)+k];
			}
			auto calced = round2(s, oip->audio_ch);
			ofs.write(reinterpret_cast<const char *>(&calced), wf.nBlockAlign);
		}
	}
	return true;
}

template <typename T>
static bool
write_dup(OUTPUT_INFO *oip, WAVEFORMATEX &wf, std::ofstream &ofs, int len)
{
	int readed = 0;
	for (auto i=0; i<oip->audio_n; i+=readed) {
		if ( oip->func_is_abort() ) { break; }
		oip->func_rest_time_disp(i, oip->audio_n);
		const auto org = static_cast<const T *>( oip->func_get_audio(i, len, &readed, config.format) );
		if ( readed == 0 ) { break; }
		for (auto j=0; j<readed; j++) {
			for (auto k=0; k<wf.nChannels; k++) {
				ofs.write(reinterpret_cast<const char *>(&org[j]), wf.nBlockAlign);
			}
		}
	}
	return true;
}

template <typename T>
static bool
write_top2(OUTPUT_INFO *oip, WAVEFORMATEX &wf, std::ofstream &ofs, int len)
{
	int readed = 0;
	for (auto i=0; i<oip->audio_n; i+=readed) {
		if ( oip->func_is_abort() ) { break; }
		oip->func_rest_time_disp(i, oip->audio_n);
		const auto org = static_cast<const T *>( oip->func_get_audio(i, len, &readed, config.format) );
		if ( readed == 0 ) { break; }
		for (auto j=0; j<readed; j++) {
			for (auto k=0; k<wf.nChannels; k++) {
				ofs.write(reinterpret_cast<const char *>(&org[j*(oip->audio_ch)+k]), wf.nBlockAlign);
			}
		}
	}
	return true;
}

static bool
func_output(OUTPUT_INFO *oip)
{
	// ヘッダの構築と書き込み
	WAVEFORMATEX wf;
	DWORD datasize = build_header(oip, wf);
	
	std::ofstream ofs(oip->savefile, std::ios::binary);
	if (!ofs.is_open()) { return false; }
	
	write_header(wf, ofs, datasize);
	
	// データの書き込み
	bool ret;
	if ( wf.nChannels == oip->audio_ch ) { // プロジェクトと出力のチャンネル数が同じならそのまま
		ret = write_asis(oip, wf, ofs, oip->audio_rate);
	} else if ( wf.nChannels == 1 ) { // 複数チャンネル -> モノラル：すべてのチャンネルの平均にマージする
		if ( config.format == WAVE_FORMAT_IEEE_FLOAT ) {
			ret = write_merge_f(oip, wf, ofs, oip->audio_rate);
		} else {
			ret = write_merge_s(oip, wf, ofs, oip->audio_rate);
		}
	} else if ( oip->audio_ch == 1 ) { // モノラル -> ステレオ：左右に同じ値を入れる
		if ( config.format == WAVE_FORMAT_IEEE_FLOAT ) {
			ret = write_dup<float>(oip, wf, ofs, oip->audio_rate);
		} else {
			ret = write_dup<std::int16_t>(oip, wf, ofs, oip->audio_rate);
		}
	} else { // 3チャンネル以上 -> ステレオ：先頭2チャンネルだけを出力する
		if ( config.format == WAVE_FORMAT_IEEE_FLOAT ) {
			ret = write_top2<float>(oip, wf, ofs, oip->audio_rate);
		} else {
			ret = write_top2<std::int16_t>(oip, wf, ofs, oip->audio_rate);
		}
	}
	
	ofs.close();
	return ret;
}

static WORD fmt_now=0u, nch_now=0u;

EXTERN_C void
InitializeConfig(CONFIG_HANDLE *ch)
{
	config_path = std::wstring(ch->app_data_path);
	config_path += L"Plugin\\";
	config_path += config_filename;
	load_config();
}

static INT_PTR CALLBACK
func_config_proc(HWND hdlg, UINT umsg, WPARAM wparam, LPARAM lparam)
{
	if ( umsg == WM_INITDIALOG ) {
		fmt_now = config.format;
		nch_now = config.n_ch;
		if ( fmt_now == WAVE_FORMAT_IEEE_FLOAT ) {
			SendMessage(GetDlgItem(hdlg, IDC_32F), BM_SETCHECK, TRUE, 0);
		} else if ( fmt_now == WAVE_FORMAT_PCM ) {
			SendMessage(GetDlgItem(hdlg, IDC_16S), BM_SETCHECK, TRUE, 0);
		}
		if ( nch_now == 2u ) {
			SendMessage(GetDlgItem(hdlg, IDC_STEREO), BM_SETCHECK, TRUE, 0);
		} else if ( nch_now == 1u ) {
			SendMessage(GetDlgItem(hdlg, IDC_MONAURAL), BM_SETCHECK, TRUE, 0);
		} else if ( nch_now == 0u ) {
			SendMessage(GetDlgItem(hdlg, IDC_AUTO), BM_SETCHECK, TRUE, 0);
		}
		return TRUE;
	} else if ( umsg == WM_DESTROY ) {
		return TRUE;
	} else if ( umsg == WM_COMMAND ) {
		WORD lwparam = LOWORD(wparam);
		if ( lwparam == IDCANCEL ) {
			EndDialog(hdlg, lwparam);
		} else if ( lwparam == IDOK ) {
			config.format = fmt_now;
			config.n_ch = nch_now;
			EndDialog(hdlg, lwparam);
		} else if ( lwparam == IDC_32F ) {
			fmt_now = WAVE_FORMAT_IEEE_FLOAT;
		} else if ( lwparam == IDC_16S ) {
			fmt_now = WAVE_FORMAT_PCM;
		} else if ( lwparam == IDC_STEREO ) {
			nch_now = 2u;
		} else if ( lwparam == IDC_MONAURAL ) {
			nch_now = 1u;
		} else if ( lwparam == IDC_AUTO ) {
			nch_now = 0u;
		}
		return TRUE;
	}
	return FALSE;
}

static bool
func_config(HWND hwnd, HINSTANCE dll_hinst)
{
	DialogBoxW(dll_hinst, L"CONFIG", hwnd, func_config_proc);
	save_config();
	return true;
}

static bool
func_load_project_config(PROJECT_FILE* project)
{
	return project->get_param_binary("CONFIG", reinterpret_cast<void *>(&config), static_cast<int>(sizeof(config)));
}

static bool
func_save_project_config(PROJECT_FILE* project)
{
	project->set_param_binary("CONFIG", reinterpret_cast<void *>(&config), static_cast<int>(sizeof(config)));
	return true;
}

static void
load_config()
{
	std::ifstream ifs(config_path.c_str(), std::ios::binary);
	if ( !ifs.is_open() ) { return; }
	ifs.seekg(0, std::ios::end);
	std::streamsize size = ifs.tellg();
	ifs.seekg(0, std::ios::beg);
	if ( size == sizeof(config) ) {
		ifs.read(reinterpret_cast<char *>(&config), size);
	}
	ifs.close();
}

static void
save_config()
{
	std::ofstream ofs(config_path.c_str(), std::ios::binary);
	if ( !ofs.is_open() ) { return; }
	ofs.write(reinterpret_cast<const char *>(&config), sizeof(config));
	ofs.close();
}

static LPCWSTR
func_get_config_text()
{
	static std::wstring config_text;
	config_text = std::wstring(
		config.format == WAVE_FORMAT_IEEE_FLOAT ? L"32bit float / " :
			( config.format == WAVE_FORMAT_PCM ? L"16bit short / " : L"error / " ) );
	config_text += ( config.n_ch == 0 ? L"オート" : ( config.n_ch == 1 ? L"モノラル" :
			( config.n_ch == 2 ? L"ステレオ" : L"error" ) ) );
	return config_text.c_str();
}
