#include "output2.hpp"
#include "config2.hpp"
#include <mmreg.h>
#include <format>
#include <fstream>
#include "resource.hpp"
#include "version.hpp"

static bool func_output(OUTPUT_INFO *oip);
static bool func_config(HWND hwnd, HINSTANCE hinst);
static void load_config();
static void save_config();
static LPCWSTR func_get_config_text();

static struct {
	WORD format;
	WORD n_ch;
} config = {WAVE_FORMAT_PCM, 0};
static const std::wstring auo_filename = L"wav_output_ks.auo2";
static const std::wstring config_filename = L"wav_output_ks.config";
static std::wstring config_path;
constexpr static const DWORD fccRIFF = mmioFOURCC('R', 'I', 'F', 'F');
constexpr static const DWORD fccWAVE = mmioFOURCC('W', 'A', 'V', 'E');
constexpr static const DWORD fccfmt = mmioFOURCC('f', 'm', 't', ' ');
constexpr static const DWORD fccdata = mmioFOURCC('d', 'a', 't', 'a');

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
	};
	return &opt;
}

static std::int16_t
round2(int sum, int n)
{
	int r = sum % n;
	if ( r*2 < n ) {
		return static_cast<std::int16_t>((sum-r)/n);
	} else {
		return static_cast<std::int16_t>((sum-r)/n+1);
	}
}

static bool
func_output(OUTPUT_INFO *oip)
{
	// ヘッダの構築
	WAVEFORMATEX wf;
	wf.wFormatTag = config.format;
	if ( config.n_ch == 0 ) {
		wf.nChannels = static_cast<WORD>(oip->audio_ch);
	} else {
		wf.nChannels = config.n_ch;
	}
	wf.nSamplesPerSec = oip->audio_rate;
	if ( config.format == WAVE_FORMAT_IEEE_FLOAT ) {
		wf.wBitsPerSample = 32;
	} else if ( config.format == WAVE_FORMAT_PCM ) {
		wf.wBitsPerSample = 16;
	} else {
		return false;
	}
	wf.nBlockAlign = wf.nChannels * ( wf.wBitsPerSample / 8 );
	wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
	DWORD datasize = wf.nBlockAlign * oip->audio_n + 36;
	
	// 変換用のメモリ領域確保
	int readlength = oip->audio_rate;
	float *calced_f=nullptr;
	std::int16_t *calced_s=nullptr;
	if ( wf.nChannels == 1 && 1 < oip->audio_ch ) {
		if ( config.format == WAVE_FORMAT_IEEE_FLOAT ) {
			try {
				calced_f = new float[readlength];
			} catch ( std::bad_alloc &e ) {
				return false;
			}
		} else {
			try {
				calced_s = new std::int16_t[readlength];
			} catch ( std::bad_alloc &e ) {
				return false;
			}
		}
	} else if ( wf.nChannels == 2 && wf.nChannels != oip->audio_ch ) {
		if ( config.format == WAVE_FORMAT_IEEE_FLOAT ) {
			try {
				calced_f = new float[readlength*2];
			} catch ( std::bad_alloc &e ) {
				return false;
			}
		} else {
			try {
				calced_s = new std::int16_t[readlength*2];
			} catch ( std::bad_alloc &e ) {
				return false;
			}
		}
	}
	
	std::ofstream ofs(oip->savefile, std::ios::binary);
	if (!ofs.is_open()) { return false; }
	
	// ヘッダの書き込み
	ofs.write(reinterpret_cast<const char *>(&fccRIFF), sizeof(DWORD));
	ofs.write(reinterpret_cast<const char *>(&datasize), sizeof(DWORD));
	ofs.write(reinterpret_cast<const char *>(&fccWAVE), sizeof(DWORD));
	
	ofs.write(reinterpret_cast<const char *>(&fccfmt), sizeof(DWORD));
	constexpr static const DWORD wfsize = 16;
	ofs.write(reinterpret_cast<const char *>(&wfsize), sizeof(DWORD));
	ofs.write(reinterpret_cast<const char *>(&wf), wfsize);
	
	datasize -= 36;
	ofs.write(reinterpret_cast<const char *>(&fccdata), sizeof(DWORD));
	ofs.write(reinterpret_cast<const char *>(&datasize), sizeof(DWORD));
	
	// データの書き込み
	for ( int i=0; i<oip->audio_n; ) {
		if ( oip->func_is_abort() ) { break; }
		oip->func_rest_time_disp(i, oip->audio_n);
		int readed = 0;
		char *data = static_cast<char *>( oip->func_get_audio(i, readlength, &readed, config.format) );
		if ( readed == 0 ) { break; }
		if ( wf.nChannels == oip->audio_ch ) { // プロジェクトと出力のチャンネル数が同じならそのまま
			ofs.write(data, readed*wf.nBlockAlign);
		} else if ( wf.nChannels == 1 ) { // 複数チャンネル -> モノラル：すべてのチャンネルの平均にマージする
			if ( config.format == WAVE_FORMAT_IEEE_FLOAT ) {
				float *org = reinterpret_cast<float *>(data);
				for (auto j=0; j<readed; j++) {
					float f = 0.0f;
					for (auto k=0; k<(oip->audio_ch); k++) {
						f += org[j*(oip->audio_ch)+k];
					}
					calced_f[j] = f/static_cast<float>(oip->audio_ch);
				}
				ofs.write(reinterpret_cast<const char*>(calced_f), readed*wf.nBlockAlign);
			} else {
				std::int16_t *org = reinterpret_cast<std::int16_t *>(data);
				for (auto j=0; j<readed; j++) {
					int s = 0;
					for (auto k=0; k<(oip->audio_ch); k++) {
						s += org[j*(oip->audio_ch)+k];
					}
					calced_s[j] = round2(s, oip->audio_ch);
				}
				ofs.write(reinterpret_cast<const char*>(calced_s), readed*wf.nBlockAlign);
			}
		} else if ( oip->audio_ch == 1 ) { // モノラル -> ステレオ：左右に同じ値を入れる
			if ( config.format == WAVE_FORMAT_IEEE_FLOAT ) {
				float *org = reinterpret_cast<float *>(data);
				for (auto j=0; j<readed; j++) {
					for (auto k=0; k<wf.nChannels; k++) {
						calced_f[j*wf.nChannels+k] = org[j];
					}
				}
				ofs.write(reinterpret_cast<const char*>(calced_f), readed*wf.nBlockAlign);
			} else {
				std::int16_t *org = reinterpret_cast<std::int16_t *>(data);
				for (auto j=0; j<readed; j++) {
					for (auto k=0; k<wf.nChannels; k++) {
						calced_s[j*wf.nChannels+k] = org[j];
					}
				}
				ofs.write(reinterpret_cast<const char*>(calced_s), readed*wf.nBlockAlign);
			}
		} else { // 3チャンネル以上 -> ステレオ：先頭2チャンネルだけを出力する
			if ( config.format == WAVE_FORMAT_IEEE_FLOAT ) {
				float *org = reinterpret_cast<float *>(data);
				for (auto j=0; j<readed; j++) {
					for (auto k=0; k<wf.nChannels; k++) {
						calced_f[j*wf.nChannels+k] = org[j*(oip->audio_ch)+k];
					}
				}
				ofs.write(reinterpret_cast<const char*>(calced_f), readed*wf.nBlockAlign);
			} else {
				std::int16_t *org = reinterpret_cast<std::int16_t *>(data);
				for (auto j=0; j<readed; j++) {
					for (auto k=0; k<wf.nChannels; k++) {
						calced_s[j*wf.nChannels+k] = org[j*(oip->audio_ch)+k];
					}
				}
				ofs.write(reinterpret_cast<const char*>(calced_s), readed*wf.nBlockAlign);
			}
		}
		i += readed;
	}
	
	delete[] calced_f;
	delete[] calced_s;
	
	ofs.close();
	return true;
}

static WORD fmt_now=0, nch_now=-1;

EXTERN_C void
InitializeConfig(CONFIG_HANDLE *ch)
{
	config_path = std::format(L"{}Plugin\\{}", ch->app_data_path, config_filename);
	load_config();
}

static LRESULT CALLBACK
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
		if ( nch_now == 2 ) {
			SendMessage(GetDlgItem(hdlg, IDC_STEREO), BM_SETCHECK, TRUE, 0);
		} else if ( nch_now == 1 ) {
			SendMessage(GetDlgItem(hdlg, IDC_MONAURAL), BM_SETCHECK, TRUE, 0);
		} else if ( nch_now == 0 ) {
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
			nch_now = 2;
		} else if ( lwparam == IDC_MONAURAL ) {
			nch_now = 1;
		} else if ( lwparam == IDC_AUTO ) {
			nch_now = 0;
		}
		return TRUE;
	}
	return FALSE;
}

static bool
func_config(HWND hwnd, HINSTANCE dll_hinst)
{
	DialogBoxW(dll_hinst, L"CONFIG", hwnd, reinterpret_cast<DLGPROC>(func_config_proc));
	save_config();
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
	config_text = std::format(
		L"{} / {}",
		( config.format == WAVE_FORMAT_IEEE_FLOAT ? L"32bit float" :
			( config.format == WAVE_FORMAT_PCM ? L"16bit short" : L"error" ) ),
		( config.n_ch == 0 ? L"オート" : ( config.n_ch == 1 ? L"モノラル" :
			( config.n_ch == 2 ? L"ステレオ" : L"error" ) ) )
	);
	return config_text.c_str();
}
