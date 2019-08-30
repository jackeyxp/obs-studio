#include <obs-module.h>
#include <util/threading.h>
#include <util/platform.h>
#include <util/darray.h>
#include <util/dstr.h>

#define do_log(level, format, ...)               \
	blog(level, "[slideshow: '%s'] " format, \
	     obs_source_get_name(ss->source), ##__VA_ARGS__)

#define warn(format, ...) do_log(LOG_WARNING, format, ##__VA_ARGS__)

/* clang-format off */

#define S_TR_SPEED                     "transition_speed"
#define S_CUSTOM_SIZE                  "use_custom_size"
#define S_SLIDE_TIME                   "slide_time"
#define S_TRANSITION                   "transition"
#define S_RANDOMIZE                    "randomize"
#define S_LOOP                         "loop"
#define S_HIDE                         "hide"
#define S_FILES                        "files"
#define S_PPT_FILE                     "ppt_file"
#define S_BEHAVIOR                     "playback_behavior"
#define S_BEHAVIOR_STOP_RESTART        "stop_restart"
#define S_BEHAVIOR_PAUSE_UNPAUSE       "pause_unpause"
#define S_BEHAVIOR_ALWAYS_PLAY         "always_play"
#define S_MODE                         "slide_mode"
#define S_MODE_AUTO                    "mode_auto"
#define S_MODE_MANUAL                  "mode_manual"

#define TR_CUT                         "cut"
#define TR_FADE                        "fade"
#define TR_SWIPE                       "swipe"
#define TR_SLIDE                       "slide"

#define T_(text) obs_module_text("SlideShow." text)
#define T_TR_SPEED                     T_("TransitionSpeed")
#define T_CUSTOM_SIZE                  T_("CustomSize")
#define T_CUSTOM_SIZE_AUTO             T_("CustomSize.Auto")
#define T_SLIDE_TIME                   T_("SlideTime")
#define T_TRANSITION                   T_("Transition")
#define T_RANDOMIZE                    T_("Randomize")
#define T_LOOP                         T_("Loop")
#define T_HIDE                         T_("HideWhenDone")
#define T_FILES                        T_("Files")
#define T_BEHAVIOR                     T_("PlaybackBehavior")
#define T_BEHAVIOR_STOP_RESTART        T_("PlaybackBehavior.StopRestart")
#define T_BEHAVIOR_PAUSE_UNPAUSE       T_("PlaybackBehavior.PauseUnpause")
#define T_BEHAVIOR_ALWAYS_PLAY         T_("PlaybackBehavior.AlwaysPlay")
#define T_MODE                         T_("SlideMode")
#define T_MODE_AUTO                    T_("SlideMode.Auto")
#define T_MODE_MANUAL                  T_("SlideMode.Manual")

#define T_TR_(text) obs_module_text("SlideShow.Transition." text)
#define T_TR_CUT                       T_TR_("Cut")
#define T_TR_FADE                      T_TR_("Fade")
#define T_TR_SWIPE                     T_TR_("Swipe")
#define T_TR_SLIDE                     T_TR_("Slide")

/* clang-format on */

/* ------------------------------------------------------------------------- */

extern uint64_t image_source_get_memory_usage(void *data);

#define BYTES_TO_MBYTES (1024 * 1024)
#define MAX_MEM_USAGE (250 * BYTES_TO_MBYTES)

struct image_file_data {
	char *path;
	char * name;
	int    slide_id;
	obs_source_t *source;
};

enum behavior {
	BEHAVIOR_STOP_RESTART,
	BEHAVIOR_PAUSE_UNPAUSE,
	BEHAVIOR_ALWAYS_PLAY,
};

struct slideshow {
	obs_source_t *source;

	bool randomize;
	bool loop;
	bool restart_on_activate;
	bool pause_on_deactivate;
	bool restart;
	bool manual;
	bool hide;
	bool use_cut;
	bool paused;
	bool stop;
	float slide_time;
	uint32_t tr_speed;
	const char *tr_name;
	obs_source_t *transition;

	float elapsed;
	size_t cur_item;

	uint32_t cx;
	uint32_t cy;
	uint64_t mem_usage;

	pthread_mutex_t mutex;
	DARRAY(struct image_file_data) files;

	char * ppt_file;

	enum behavior behavior;

	obs_hotkey_id play_pause_hotkey;
	obs_hotkey_id restart_hotkey;
	obs_hotkey_id stop_hotkey;
	obs_hotkey_id next_hotkey;
	obs_hotkey_id prev_hotkey;
};

static obs_source_t *get_transition(struct slideshow *ss)
{
	obs_source_t *tr;

	pthread_mutex_lock(&ss->mutex);
	tr = ss->transition;
	obs_source_addref(tr);
	pthread_mutex_unlock(&ss->mutex);

	return tr;
}

static obs_source_t *get_source(struct darray *array, const char *path)
{
	DARRAY(struct image_file_data) files;
	obs_source_t *source = NULL;

	files.da = *array;

	for (size_t i = 0; i < files.num; i++) {
		const char *cur_path = files.array[i].path;

		if (strcmp(path, cur_path) == 0) {
			source = files.array[i].source;
			obs_source_addref(source);
			break;
		}
	}

	return source;
}

static obs_source_t *create_source_from_file(const char *file)
{
	obs_data_t *settings = obs_data_create();
	obs_source_t *source;

	obs_data_set_string(settings, "file", file);
	obs_data_set_bool(settings, "unload", false);
	source = obs_source_create_private("image_source", NULL, settings);

	obs_data_release(settings);
	return source;
}

static void free_files(struct darray *array)
{
	DARRAY(struct image_file_data) files;
	files.da = *array;

	for (size_t i = 0; i < files.num; i++) {
		bfree(files.array[i].path);
		bfree(files.array[i].name);
		obs_source_release(files.array[i].source);
	}

	da_free(files);
}

static inline size_t random_file(struct slideshow *ss)
{
	return (size_t)rand() % ss->files.num;
}

/* ------------------------------------------------------------------------- */

static const char *ss_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("SlideShow");
}

static const char * get_cur_item_name(struct slideshow *ss)
{
	// 总数有效，并且当前编号小于总数，直接返回元素名称...
	if (ss->files.num > 0 && ss->cur_item < ss->files.num) {
		return ss->files.array[ss->cur_item].name;
	}
	// 其它无效情况，直接返回默认元素名称...
	return obs_module_text("SlideShow.ItemName");
}

static int8_t sDigitMask[] =
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //0-9
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //10-19 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //20-29
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //30-39
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, //40-49 //stop on every character except a number
	1, 1, 1, 1, 1, 1, 1, 1, 0, 0, //50-59
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //60-69 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //70-79
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //80-89
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //90-99
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //100-109
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //110-119
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //120-129
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //130-139
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //140-149
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //150-159
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //160-169
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //170-179
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //180-189
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //190-199
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //200-209
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //210-219
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //220-229
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //230-239
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //240-249
	0, 0, 0, 0, 0, 0			 //250-255
};

// 解析出文件名中的序号，没有解析到，返回-1
int parse_slide(const char *path)
{
	int slide_id = 0;
	uint8_t * fStartGet = (uint8_t*)path;
	uint8_t * fEndGet = fStartGet + strlen(path);
	// 遍历字符串，直到遇到数字就停止遍历...
	while ((fStartGet < fEndGet) && (!sDigitMask[*fStartGet])) {
		fStartGet++;
	}
	// 如果开始大于或等于结束，说明没有找到数字，返回-1
	if (fStartGet >= fEndGet)
		return -1;
	// 从当前位置开始，解析出数字内容...
	while ((fStartGet < fEndGet) && (*fStartGet >= '0') && (*fStartGet <= '9')) {
		slide_id = (slide_id * 10) + (*fStartGet - '0');
		fStartGet++;
	}
	// 返回有效序号...
	return slide_id;
}

// 数组元素按照slide_id从小到大进行有序排列...
static int file_slide_compare(const void *first, const void *second)
{
	int diff = ((struct image_file_data*)first)->slide_id - ((struct image_file_data*)second)->slide_id;
	return diff < 0 ? -1 : (diff > 0 ? 1 : 0);
}

// 新增排序编号，编号为-1，表示不排序...
static void add_file(struct slideshow *ss, struct darray *array,
		const char *path, uint32_t *cx, uint32_t *cy, int slide_id)
{
	DARRAY(struct image_file_data) new_files;
	struct image_file_data data;
	obs_source_t *new_source;

	new_files.da = *array;

	pthread_mutex_lock(&ss->mutex);
	new_source = get_source(&ss->files.da, path);
	pthread_mutex_unlock(&ss->mutex);

	if (!new_source)
		new_source = get_source(&new_files.da, path);
	if (!new_source)
		new_source = create_source_from_file(path);

	if (new_source) {
		uint32_t new_cx = obs_source_get_width(new_source);
		uint32_t new_cy = obs_source_get_height(new_source);

		data.name = bstrdup(obs_module_text("SlideShow.ItemName"));
		data.path = bstrdup(path);
		data.source = new_source;
		data.slide_id = slide_id;
		da_push_back(new_files, &data);

		if (new_cx > *cx)
			*cx = new_cx;
		if (new_cy > *cy)
			*cy = new_cy;

		void *source_data = obs_obj_get_data(new_source);
		ss->mem_usage += image_source_get_memory_usage(source_data);
	}

	*array = new_files.da;
}

static bool valid_extension(const char *ext)
{
	if (!ext)
		return false;
	return astrcmpi(ext, ".bmp") == 0 || astrcmpi(ext, ".tga") == 0 ||
	       astrcmpi(ext, ".png") == 0 || astrcmpi(ext, ".jpeg") == 0 ||
	       astrcmpi(ext, ".jpg") == 0 || astrcmpi(ext, ".gif") == 0;
}

static inline bool item_valid(struct slideshow *ss)
{
	return ss->files.num && ss->cur_item < ss->files.num;
}

static void do_transition(void *data, bool to_null)
{
	struct slideshow *ss = data;
	bool valid = item_valid(ss);

	if (valid && ss->use_cut)
		obs_transition_set(ss->transition,
				   ss->files.array[ss->cur_item].source);

	else if (valid && !to_null)
		obs_transition_start(ss->transition, OBS_TRANSITION_MODE_AUTO,
				     ss->tr_speed,
				     ss->files.array[ss->cur_item].source);

	else
		obs_transition_start(ss->transition, OBS_TRANSITION_MODE_AUTO,
				     ss->tr_speed, NULL);
}

static void do_update_user(void *data, obs_data_t *settings, const char * lpFile, const char * lpUser)
{
	int nIndexItem = -1;
	struct slideshow *ss = data;
	pthread_mutex_lock(&ss->mutex);
	for (size_t i = 0; i < ss->files.num; i++) {
		const char *cur_path = ss->files.array[i].path;
		if (strcmp(lpFile, cur_path) == 0) {
			bfree(ss->files.array[i].name);
			ss->files.array[i].name = bstrdup(lpUser);
			nIndexItem = i;
			break;
		}
	}
	pthread_mutex_unlock(&ss->mutex);
	// 更新用户名称到配置当中，并通知上层数据源发生变化...
	if (nIndexItem >= 0 && ss->cur_item == nIndexItem) {
		const char *cur_name = ss->files.array[nIndexItem].name;
		obs_data_set_string(settings, "item_name", cur_name);
		obs_source_updated(ss->source);
	}
}

static void do_update_size(void *data)
{
	struct slideshow *ss = data;
	pthread_mutex_lock(&ss->mutex);
	uint32_t cx = 0; uint32_t cy = 0;
	for (size_t i = 0; i < ss->files.num; i++) {
		obs_source_t * source = ss->files.array[i].source;
		uint32_t new_cx = obs_source_get_width(source);
		uint32_t new_cy = obs_source_get_height(source);
		if (new_cx > cx) cx = new_cx;
		if (new_cy > cy) cy = new_cy;
	}
	// 保存遍历后的最大尺寸...
	ss->cx = cx; ss->cy = cy;
	obs_transition_set_size(ss->transition, cx, cy);
	pthread_mutex_unlock(&ss->mutex);
}

static void do_screen_change(void *data, obs_data_t *settings)
{
	struct slideshow *ss = data;
	int nScreenID = (int)obs_data_get_int(settings, "screen_id");
	const char * lpUser = obs_data_get_string(settings, "screen_user");
	const char * lpFile = obs_data_get_string(settings, "screen_file");

	obs_data_set_bool(settings, "screen_change", false);
	
	obs_source_t * new_source = NULL;
	pthread_mutex_lock(&ss->mutex);
	// 用文件路径查找图片对象，注意释放引用计数器...
	new_source = get_source(&ss->files.da, lpFile);
	obs_source_release(new_source);
	pthread_mutex_unlock(&ss->mutex);
	// 强制更新已经存在的图片数据源...
	if (new_source != NULL) {
		obs_data_t * lpImageSettings = obs_source_get_settings(new_source);
		obs_data_set_string(lpImageSettings, "file", lpFile);
		obs_data_set_bool(lpImageSettings, "unload", false);
		obs_source_update(new_source, lpImageSettings);
		obs_data_release(lpImageSettings);
		// 寻找最大尺寸更新到幻灯对象当中...
		do_update_size(data);
		// 找到指定的文件名，更新用户名称...
		if (lpFile != NULL && lpUser != NULL) {
			do_update_user(data, settings, lpFile, lpUser);
		}
		return;
	}
	// 如果当前更新文件没有在文件队列当中，需要新建...
	struct image_file_data item;
	new_source = create_source_from_file(lpFile);
	if (new_source == NULL) return;
	assert(new_source != NULL);
	// 新建图像追加到列表末尾...
	item.name = bstrdup(lpUser);
	item.path = bstrdup(lpFile);
	item.source = new_source;
	item.slide_id = nScreenID;
	da_push_back(ss->files, &item);
	// 对读取到的文件列表，进行排序，依据slide_id排序 => 从小到大...
	qsort(ss->files.array, ss->files.num, sizeof(struct image_file_data), file_slide_compare);
	// 寻找最大尺寸更新到幻灯对象当中...
	do_update_size(data);
	// 只有一个文件，强制显示...
	if (ss->files.num == 1) {
		ss->cur_item = 0;
		do_transition(ss, false);
	}
	// 保存当前编号和总数到配置当中 => 方便外层界面使用...
	obs_data_set_string(settings, "item_name", get_cur_item_name(ss));
	obs_data_set_int(settings, "cur_item", ss->cur_item);
	obs_data_set_int(settings, "file_num", ss->files.num);
	// 进行数据源的上层通知，幻灯片已经完全加载更新完毕...
	obs_source_updated(ss->source);
}

static void ss_update(void *data, obs_data_t *settings)
{
	DARRAY(struct image_file_data) new_files;
	DARRAY(struct image_file_data) old_files;
	obs_source_t *new_tr = NULL;
	obs_source_t *old_tr = NULL;
	struct slideshow *ss = data;
	obs_data_array_t *array;
	const char *tr_name;
	const char *ppt_file;
	uint32_t new_duration;
	uint32_t new_speed;
	uint32_t cx = 0;
	uint32_t cy = 0;
	size_t count;
	const char *behavior;
	const char *mode;

	/* ------------------------------------- */
	/* get settings data */

	// 对学生屏幕分享进行拦截操作...
	bool bIsScreen = obs_data_get_bool(settings, "screen_slide");
	bool bIsChanged = obs_data_get_bool(settings, "screen_change");
	if (bIsChanged && bIsChanged) {
		do_screen_change(data, settings);
		return;
	}

	ppt_file = obs_data_get_string(settings, S_PPT_FILE);
	if (ss->ppt_file) {
		bfree(ss->ppt_file);
	}
	ss->ppt_file = bstrdup(ppt_file);

	da_init(new_files);

	behavior = obs_data_get_string(settings, S_BEHAVIOR);
	if (astrcmpi(behavior, S_BEHAVIOR_PAUSE_UNPAUSE) == 0)
		ss->behavior = BEHAVIOR_PAUSE_UNPAUSE;
	else if (astrcmpi(behavior, S_BEHAVIOR_ALWAYS_PLAY) == 0)
		ss->behavior = BEHAVIOR_ALWAYS_PLAY;
	else /* S_BEHAVIOR_STOP_RESTART */
		ss->behavior = BEHAVIOR_STOP_RESTART;

	mode = obs_data_get_string(settings, S_MODE);

	ss->manual = (astrcmpi(mode, S_MODE_MANUAL) == 0);

	tr_name = obs_data_get_string(settings, S_TRANSITION);
	if (astrcmpi(tr_name, TR_CUT) == 0)
		tr_name = "cut_transition";
	else if (astrcmpi(tr_name, TR_SWIPE) == 0)
		tr_name = "swipe_transition";
	else if (astrcmpi(tr_name, TR_SLIDE) == 0)
		tr_name = "slide_transition";
	else
		tr_name = "fade_transition";

	ss->randomize = obs_data_get_bool(settings, S_RANDOMIZE);
	ss->loop = obs_data_get_bool(settings, S_LOOP);
	ss->hide = obs_data_get_bool(settings, S_HIDE);

	if (!ss->tr_name || strcmp(tr_name, ss->tr_name) != 0)
		new_tr = obs_source_create_private(tr_name, NULL, NULL);

	new_duration = (uint32_t)obs_data_get_int(settings, S_SLIDE_TIME);
	new_speed = (uint32_t)obs_data_get_int(settings, S_TR_SPEED);

	array = obs_data_get_array(settings, S_FILES);
	count = obs_data_array_count(array);

	/* ------------------------------------- */
	/* create new list of sources */

	ss->mem_usage = 0;

	for (size_t i = 0; i < count; i++) {
		obs_data_t *item = obs_data_array_item(array, i);
		const char *path = obs_data_get_string(item, "value");
		os_dir_t *dir = os_opendir(path);

		if (dir) {
			struct dstr dir_path = {0};
			struct os_dirent *ent;

			for (;;) {
				const char *ext;
				int slide_id = -1;

				ent = os_readdir(dir);
				if (!ent)
					break;
				if (ent->directory)
					continue;

				ext = os_get_path_extension(ent->d_name);
				if (!valid_extension(ext))
					continue;

				// 解析出文件名中的序号，没有解析到，返回-1
				slide_id = parse_slide(ent->d_name);

				dstr_copy(&dir_path, path);
				dstr_cat_ch(&dir_path, '/');
				dstr_cat(&dir_path, ent->d_name);
				add_file(ss, &new_files.da, dir_path.array,
						&cx, &cy, slide_id);

				if (ss->mem_usage >= MAX_MEM_USAGE)
					break;
			}

			dstr_free(&dir_path);
			os_closedir(dir);
		} else {
			add_file(ss, &new_files.da, path, &cx, &cy, -1);
		}

		obs_data_release(item);

		if (ss->mem_usage >= MAX_MEM_USAGE)
			break;
	}

	// 对读取到的文件列表，进行排序，依据slide_id排序 => 从小到大...
	qsort(new_files.array, new_files.num, sizeof(struct image_file_data), file_slide_compare);

	/* ------------------------------------- */
	/* update settings data */

	pthread_mutex_lock(&ss->mutex);

	old_files.da = ss->files.da;
	ss->files.da = new_files.da;
	if (new_tr) {
		old_tr = ss->transition;
		ss->transition = new_tr;
	}

	if (strcmp(tr_name, "cut_transition") != 0) {
		if (new_duration < 100)
			new_duration = 100;

		new_duration += new_speed;
	} else {
		if (new_duration < 50)
			new_duration = 50;
	}

	ss->tr_speed = new_speed;
	ss->tr_name = tr_name;
	ss->slide_time = (float)new_duration / 1000.0f;

	pthread_mutex_unlock(&ss->mutex);

	/* ------------------------------------- */
	/* clean up and restart transition */

	if (old_tr)
		obs_source_release(old_tr);
	free_files(&old_files.da);

	/* ------------------------- */

	const char *res_str = obs_data_get_string(settings, S_CUSTOM_SIZE);
	bool aspect_only = false, use_auto = true;
	int cx_in = 0, cy_in = 0;

	if (strcmp(res_str, T_CUSTOM_SIZE_AUTO) != 0) {
		int ret = sscanf(res_str, "%dx%d", &cx_in, &cy_in);
		if (ret == 2) {
			aspect_only = false;
			use_auto = false;
		} else {
			ret = sscanf(res_str, "%d:%d", &cx_in, &cy_in);
			if (ret == 2) {
				aspect_only = true;
				use_auto = false;
			}
		}
	}

	if (!use_auto) {
		double cx_f = (double)cx;
		double cy_f = (double)cy;

		double old_aspect = cx_f / cy_f;
		double new_aspect = (double)cx_in / (double)cy_in;

		if (aspect_only) {
			if (fabs(old_aspect - new_aspect) > EPSILON) {
				if (new_aspect > old_aspect)
					cx = (uint32_t)(cy_f * new_aspect);
				else
					cy = (uint32_t)(cx_f / new_aspect);
			}
		} else {
			cx = (uint32_t)cx_in;
			cy = (uint32_t)cy_in;
		}
	}

	/* ------------------------- */

	ss->cx = cx;
	ss->cy = cy;
	ss->cur_item = 0;
	ss->elapsed = 0.0f;
	obs_transition_set_size(ss->transition, cx, cy);
	obs_transition_set_alignment(ss->transition, OBS_ALIGN_CENTER);
	obs_transition_set_scale_type(ss->transition,
				      OBS_TRANSITION_SCALE_ASPECT);

	if (ss->randomize && ss->files.num)
		ss->cur_item = random_file(ss);
	if (new_tr)
		obs_source_add_active_child(ss->source, new_tr);
	if (ss->files.num)
		do_transition(ss, false);

	obs_data_array_release(array);

	// 保存当前编号和总数到配置当中 => 方便外层界面使用...
	obs_data_set_string(settings, "item_name", get_cur_item_name(ss));
	obs_data_set_int(settings, "cur_item", ss->cur_item);
	obs_data_set_int(settings, "file_num", ss->files.num);
	// 进行数据源的上层通知，幻灯片已经完全加载更新完毕...
	obs_source_updated(ss->source);
}

static void ss_play_pause(void *data)
{
	struct slideshow *ss = data;

	ss->paused = !ss->paused;
	ss->manual = ss->paused;
}

static void ss_restart(void *data)
{
	struct slideshow *ss = data;

	ss->elapsed = 0.0f;
	ss->cur_item = 0;

	obs_transition_set(ss->transition,
			   ss->files.array[ss->cur_item].source);

	ss->stop = false;
	ss->paused = false;
}

static void ss_stop(void *data)
{
	struct slideshow *ss = data;

	ss->elapsed = 0.0f;
	ss->cur_item = 0;

	do_transition(ss, true);
	ss->stop = true;
	ss->paused = false;
}

static void ss_next_slide(void *data)
{
	struct slideshow *ss = data;

	if (!ss->files.num || obs_transition_get_time(ss->transition) < 1.0f)
		return;

	if (++ss->cur_item >= ss->files.num)
		ss->cur_item = 0;

	// 保存当前编号和总数到配置当中 => 方便外层界面使用...
	if (ss->source != NULL) {
		obs_data_t * settings = obs_source_get_settings(ss->source);
		obs_data_set_string(settings, "item_name", get_cur_item_name(ss));
		obs_data_set_int(settings, "cur_item", ss->cur_item);
		obs_data_set_int(settings, "file_num", ss->files.num);
		obs_data_release(settings);
	}

	// 如果transition是slide或swipe变换，需要修改成left滑动方向...
	if (astrcmpi(obs_source_get_id(ss->transition), "slide_transition") == 0 ||
		astrcmpi(obs_source_get_id(ss->transition), "swipe_transition") == 0) {
		obs_data_t * lpSettings = obs_source_get_settings(ss->transition);
		obs_data_set_string(lpSettings, "direction", "left");
		obs_source_update(ss->transition, lpSettings);
		obs_data_release(lpSettings);
	}

	do_transition(ss, false);
}

static void ss_previous_slide(void *data)
{
	struct slideshow *ss = data;

	if (!ss->files.num || obs_transition_get_time(ss->transition) < 1.0f)
		return;

	if (ss->cur_item == 0)
		ss->cur_item = ss->files.num - 1;
	else
		--ss->cur_item;

	// 保存当前编号和总数到配置当中 => 方便外层界面使用...
	if (ss->source != NULL) {
		obs_data_t * settings = obs_source_get_settings(ss->source);
		obs_data_set_string(settings, "item_name", get_cur_item_name(ss));
		obs_data_set_int(settings, "cur_item", ss->cur_item);
		obs_data_set_int(settings, "file_num", ss->files.num);
		obs_data_release(settings);
	}

	// 如果transition是slide或swipe变换，需要修改成right滑动方向...
	if (astrcmpi(obs_source_get_id(ss->transition), "slide_transition") == 0 ||
		astrcmpi(obs_source_get_id(ss->transition), "swipe_transition") == 0) {
		obs_data_t * lpSettings = obs_source_get_settings(ss->transition);
		obs_data_set_string(lpSettings, "direction", "right");
		obs_source_update(ss->transition, lpSettings);
		obs_data_release(lpSettings);
	}

	do_transition(ss, false);
}

static void play_pause_hotkey(void *data, obs_hotkey_id id,
			      obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);

	struct slideshow *ss = data;

	if (pressed && obs_source_active(ss->source))
		ss_play_pause(ss);
}

static void restart_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);

	struct slideshow *ss = data;

	if (pressed && obs_source_active(ss->source))
		ss_restart(ss);
}

static void stop_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);

	struct slideshow *ss = data;

	if (pressed && obs_source_active(ss->source))
		ss_stop(ss);
}

static void next_slide_hotkey(void *data, obs_hotkey_id id,
			      obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);

	struct slideshow *ss = data;

	if (!ss->manual)
		return;

	if (pressed && obs_source_active(ss->source))
		ss_next_slide(ss);
}

static void previous_slide_hotkey(void *data, obs_hotkey_id id,
				  obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);

	struct slideshow *ss = data;

	if (!ss->manual)
		return;

	if (pressed && obs_source_active(ss->source))
		ss_previous_slide(ss);
}

static void ss_destroy(void *data)
{
	struct slideshow *ss = data;

	obs_source_release(ss->transition);
	free_files(&ss->files.da);
	bfree(ss->ppt_file);
	pthread_mutex_destroy(&ss->mutex);
	bfree(ss);
}

static void *ss_create(obs_data_t *settings, obs_source_t *source)
{
	struct slideshow *ss = bzalloc(sizeof(*ss));

	ss->source = source;

	ss->manual = false;
	ss->paused = false;
	ss->stop = false;

	ss->play_pause_hotkey = obs_hotkey_register_source(
		source, "SlideShow.PlayPause",
		obs_module_text("SlideShow.PlayPause"), play_pause_hotkey, ss);

	ss->restart_hotkey = obs_hotkey_register_source(
		source, "SlideShow.Restart",
		obs_module_text("SlideShow.Restart"), restart_hotkey, ss);

	ss->stop_hotkey = obs_hotkey_register_source(
		source, "SlideShow.Stop", obs_module_text("SlideShow.Stop"),
		stop_hotkey, ss);

	// 注意：原始代码里面这里有问题，写成了prev_hotkey...
	ss->next_hotkey = obs_hotkey_register_source(
		source, "SlideShow.NextSlide",
		obs_module_text("SlideShow.NextSlide"), next_slide_hotkey, ss);

	ss->prev_hotkey = obs_hotkey_register_source(
		source, "SlideShow.PreviousSlide",
		obs_module_text("SlideShow.PreviousSlide"),
		previous_slide_hotkey, ss);

	// 将上一页和下一页的热键编号存入当前数据源的配置当中...
	obs_data_set_int(settings, "next_hotkey", ss->next_hotkey);
	obs_data_set_int(settings, "prev_hotkey", ss->prev_hotkey);

	pthread_mutex_init_value(&ss->mutex);
	if (pthread_mutex_init(&ss->mutex, NULL) != 0)
		goto error;

	obs_source_update(source, NULL);

	UNUSED_PARAMETER(settings);
	return ss;

error:
	ss_destroy(ss);
	return NULL;
}

static void ss_video_render(void *data, gs_effect_t *effect)
{
	struct slideshow *ss = data;
	obs_source_t *transition = get_transition(ss);

	if (transition) {
		obs_source_video_render(transition);
		obs_source_release(transition);
	}

	UNUSED_PARAMETER(effect);
}

static void ss_video_tick(void *data, float seconds)
{
	struct slideshow *ss = data;

	if (!ss->transition || !ss->slide_time)
		return;

	if (ss->restart_on_activate && !ss->randomize && ss->use_cut) {
		ss->elapsed = 0.0f;
		ss->cur_item = 0;
		do_transition(ss, false);
		ss->restart_on_activate = false;
		ss->use_cut = false;
		ss->stop = false;
		return;
	}

	if (ss->pause_on_deactivate || ss->manual || ss->stop || ss->paused)
		return;

	/* ----------------------------------------------------- */
	/* fade to transparency when the file list becomes empty */
	if (!ss->files.num) {
		obs_source_t *active_transition_source =
			obs_transition_get_active_source(ss->transition);

		if (active_transition_source) {
			obs_source_release(active_transition_source);
			do_transition(ss, true);
		}
	}

	/* ----------------------------------------------------- */
	/* do transition when slide time reached                 */
	ss->elapsed += seconds;

	if (ss->elapsed > ss->slide_time) {
		ss->elapsed -= ss->slide_time;

		if (!ss->loop && ss->cur_item == ss->files.num - 1) {
			if (ss->hide)
				do_transition(ss, true);
			else
				do_transition(ss, false);

			return;
		}

		if (ss->randomize) {
			size_t next = ss->cur_item;
			if (ss->files.num > 1) {
				while (next == ss->cur_item)
					next = random_file(ss);
			}
			ss->cur_item = next;

		} else if (++ss->cur_item >= ss->files.num) {
			ss->cur_item = 0;
		}

		if (ss->files.num)
			do_transition(ss, false);
	}

	// 保存当前编号和总数到配置当中 => 方便外层界面使用...
	if (ss->source != NULL) {
		obs_data_t * settings = obs_source_get_settings(ss->source);
		obs_data_set_string(settings, "item_name", get_cur_item_name(ss));
		obs_data_set_int(settings, "cur_item", ss->cur_item);
		obs_data_set_int(settings, "file_num", ss->files.num);
		obs_data_release(settings);
	}
}

static inline bool ss_audio_render_(obs_source_t *transition, uint64_t *ts_out,
				    struct obs_source_audio_mix *audio_output,
				    uint32_t mixers, size_t channels,
				    size_t sample_rate)
{
	struct obs_source_audio_mix child_audio;
	uint64_t source_ts;

	if (obs_source_audio_pending(transition))
		return false;

	source_ts = obs_source_get_audio_timestamp(transition);
	if (!source_ts)
		return false;

	obs_source_get_audio_mix(transition, &child_audio);
	for (size_t mix = 0; mix < MAX_AUDIO_MIXES; mix++) {
		if ((mixers & (1 << mix)) == 0)
			continue;

		for (size_t ch = 0; ch < channels; ch++) {
			float *out = audio_output->output[mix].data[ch];
			float *in = child_audio.output[mix].data[ch];

			memcpy(out, in,
			       AUDIO_OUTPUT_FRAMES * MAX_AUDIO_CHANNELS *
				       sizeof(float));
		}
	}

	*ts_out = source_ts;

	UNUSED_PARAMETER(sample_rate);
	return true;
}

static bool ss_audio_render(void *data, uint64_t *ts_out,
			    struct obs_source_audio_mix *audio_output,
			    uint32_t mixers, size_t channels,
			    size_t sample_rate)
{
	struct slideshow *ss = data;
	obs_source_t *transition = get_transition(ss);
	bool success;

	if (!transition)
		return false;

	success = ss_audio_render_(transition, ts_out, audio_output, mixers,
				   channels, sample_rate);

	obs_source_release(transition);
	return success;
}

static void ss_enum_sources(void *data, obs_source_enum_proc_t cb, void *param)
{
	struct slideshow *ss = data;

	pthread_mutex_lock(&ss->mutex);
	if (ss->transition)
		cb(ss->source, ss->transition, param);
	pthread_mutex_unlock(&ss->mutex);
}

static uint32_t ss_width(void *data)
{
	struct slideshow *ss = data;
	return ss->transition ? ss->cx : 0;
}

static uint32_t ss_height(void *data)
{
	struct slideshow *ss = data;
	return ss->transition ? ss->cy : 0;
}

static void ss_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, S_TRANSITION, "slide"); //fade slide cut swipe
	obs_data_set_default_int(settings, S_SLIDE_TIME, 5000); //8000
	obs_data_set_default_int(settings, S_TR_SPEED, 800); //700
	obs_data_set_default_string(settings, S_CUSTOM_SIZE, T_CUSTOM_SIZE_AUTO);
	obs_data_set_default_string(settings, S_BEHAVIOR, S_BEHAVIOR_ALWAYS_PLAY); //S_BEHAVIOR_PAUSE_UNPAUSE
	obs_data_set_default_string(settings, S_MODE, S_MODE_MANUAL); //S_MODE_AUTO
	obs_data_set_default_bool(settings, S_LOOP, true);
	obs_data_set_default_bool(settings, "screen_slide", false);
}

//static const char *file_filter =
//	"Image files (*.bmp *.tga *.png *.jpeg *.jpg *.gif)";

static const char *file_filter =
	"PPT files (*.ppt *.pptx)";

static const char *aspects[] = {"16:9", "16:10", "4:3", "1:1"};

#define NUM_ASPECTS (sizeof(aspects) / sizeof(const char *))

static obs_properties_t *ss_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();
	struct slideshow *ss = data;
	struct obs_video_info ovi;
	struct dstr path = {0};
	obs_property_t *p;
	int cx;
	int cy;

	/* ----------------- */

	obs_get_video_info(&ovi);
	cx = (int)ovi.base_width;
	cy = (int)ovi.base_height;

	/* ----------------- */

	obs_data_t * settings = obs_source_get_settings(ss->source);
	bool bIsScreenSlide = obs_data_get_bool(settings, "screen_slide");
	obs_data_release(settings);

	// 如果不是学生屏幕分享，才显示PPT文件名称...
	if (!bIsScreenSlide) {
		struct dstr path = { 0 };
		// 读取 PPT文件 路径，设定为默认路径...
		if (ss && ss->ppt_file && *ss->ppt_file) {
			const char *slash;
			dstr_copy(&path, ss->ppt_file);
			dstr_replace(&path, "\\", "/");
			slash = strrchr(path.array, '/');
			if (slash) {
				dstr_resize(&path, slash - path.array + 1);
			}
		}
		// 追加 PPT文件 属性配置栏...
		obs_properties_add_path(ppts, S_PPT_FILE, ss_getname(NULL),
			OBS_PATH_FILE, file_filter, path.array);
		// 释放PPT路径...
		dstr_free(&path);
	}

	// 隐藏 可见性的行为 属性配置栏...
	/*p = obs_properties_add_list(ppts, S_BEHAVIOR, T_BEHAVIOR,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, T_BEHAVIOR_ALWAYS_PLAY,
			S_BEHAVIOR_ALWAYS_PLAY);
	obs_property_list_add_string(p, T_BEHAVIOR_STOP_RESTART,
			S_BEHAVIOR_STOP_RESTART);
	obs_property_list_add_string(p, T_BEHAVIOR_PAUSE_UNPAUSE,
			S_BEHAVIOR_PAUSE_UNPAUSE);*/

	// 隐藏 幻灯片模式 属性配置栏...
	/*p = obs_properties_add_list(ppts, S_MODE, T_MODE,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, T_MODE_AUTO, S_MODE_AUTO);
	obs_property_list_add_string(p, T_MODE_MANUAL, S_MODE_MANUAL);*/

	// 开启 过渡模式 属性配置栏...
	p = obs_properties_add_list(ppts, S_TRANSITION, T_TRANSITION,
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, T_TR_CUT, TR_CUT);
	obs_property_list_add_string(p, T_TR_FADE, TR_FADE);
	obs_property_list_add_string(p, T_TR_SWIPE, TR_SWIPE);
	obs_property_list_add_string(p, T_TR_SLIDE, TR_SLIDE);

	// 开启 过渡速度 隐藏 幻灯停留时间 属性配置栏...
	//obs_properties_add_int(ppts, S_SLIDE_TIME, T_SLIDE_TIME, 50, 3600000, 50);
	obs_properties_add_int(ppts, S_TR_SPEED, T_TR_SPEED, 0, 3600000, 50);
	
	// 隐藏 随机播放 属性配置栏...
	//obs_properties_add_bool(ppts, S_LOOP, T_LOOP);
	//obs_properties_add_bool(ppts, S_HIDE, T_HIDE);
	//obs_properties_add_bool(ppts, S_RANDOMIZE, T_RANDOMIZE);

	// 隐藏 边框大小/高宽比 属性配置栏...
	/*p = obs_properties_add_list(ppts, S_CUSTOM_SIZE, T_CUSTOM_SIZE,
			OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, T_CUSTOM_SIZE_AUTO, T_CUSTOM_SIZE_AUTO);
	for (size_t i = 0; i < NUM_ASPECTS; i++) {
		obs_property_list_add_string(p, aspects[i], aspects[i]);
	}
	// 追加屏幕分辨率...
	char str[32];
	snprintf(str, 32, "%dx%d", cx, cy);
	obs_property_list_add_string(p, str, str);*/

	// 读取已经配置的 图像文件 列表...
	/*if (ss && ss->files.num) {
		pthread_mutex_lock(&ss->mutex);
			struct image_file_data *last = da_end(ss->files);
			const char *slash;

			dstr_copy(&path, last->path);
			dstr_replace(&path, "\\", "/");
			slash = strrchr(path.array, '/');
		if (slash) {
				dstr_resize(&path, slash - path.array + 1);
		}
		pthread_mutex_unlock(&ss->mutex);
	}
	// 隐藏 图像文件 属性配置栏...
	//obs_properties_add_editable_list(ppts, S_FILES, T_FILES,
	//		OBS_EDITABLE_LIST_TYPE_FILES, file_filter, path.array);
	//dstr_free(&path);*/

	return ppts;
}

static void ss_activate(void *data)
{
	struct slideshow *ss = data;

	if (ss->behavior == BEHAVIOR_STOP_RESTART) {
		ss->restart_on_activate = true;
		ss->use_cut = true;
	} else if (ss->behavior == BEHAVIOR_PAUSE_UNPAUSE) {
		ss->pause_on_deactivate = false;
	}
}

static void ss_deactivate(void *data)
{
	struct slideshow *ss = data;

	if (ss->behavior == BEHAVIOR_PAUSE_UNPAUSE)
		ss->pause_on_deactivate = true;
}

struct obs_source_info slideshow_info = {
	.id = "slideshow",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW |
			OBS_SOURCE_COMPOSITE,
	.get_name = ss_getname,
	.create = ss_create,
	.destroy = ss_destroy,
	.update = ss_update,
	.activate = ss_activate,
	.deactivate = ss_deactivate,
	.video_render = ss_video_render,
	.video_tick = ss_video_tick,
	.audio_render = ss_audio_render,
	.enum_active_sources = ss_enum_sources,
	.get_width = ss_width,
	.get_height = ss_height,
	.get_defaults = ss_defaults,
	.get_properties = ss_properties,
};
