
#pragma once

typedef __int64 int64_t;

//status order is important!
#define FDFS_STORAGE_STATUS_INIT		0
#define FDFS_STORAGE_STATUS_WAIT_SYNC	1
#define FDFS_STORAGE_STATUS_SYNCING		2
#define FDFS_STORAGE_STATUS_IP_CHANGED  3
#define FDFS_STORAGE_STATUS_DELETED		4
#define FDFS_STORAGE_STATUS_OFFLINE		5
#define FDFS_STORAGE_STATUS_ONLINE		6
#define FDFS_STORAGE_STATUS_ACTIVE		7
#define FDFS_STORAGE_STATUS_RECOVERY	9
#define FDFS_STORAGE_STATUS_NONE		99

#define TRACKER_PROTO_CMD_STORAGE_JOIN              81
#define FDFS_PROTO_CMD_QUIT							82
#define TRACKER_PROTO_CMD_STORAGE_BEAT              83  //storage heart beat
#define TRACKER_PROTO_CMD_STORAGE_REPORT_DISK_USAGE 84  //report disk usage
#define TRACKER_PROTO_CMD_STORAGE_REPLICA_CHG       85  //repl new storage servers
#define TRACKER_PROTO_CMD_STORAGE_SYNC_SRC_REQ      86  //src storage require sync
#define TRACKER_PROTO_CMD_STORAGE_SYNC_DEST_REQ     87  //dest storage require sync
#define TRACKER_PROTO_CMD_STORAGE_SYNC_NOTIFY       88  //sync done notify
#define TRACKER_PROTO_CMD_STORAGE_SYNC_REPORT	    89  //report src last synced time as dest server
#define TRACKER_PROTO_CMD_STORAGE_SYNC_DEST_QUERY   79  //dest storage query sync src storage server
#define TRACKER_PROTO_CMD_STORAGE_REPORT_IP_CHANGED 78  //storage server report it's ip changed
#define TRACKER_PROTO_CMD_STORAGE_CHANGELOG_REQ     77  //storage server request storage server's changelog
#define TRACKER_PROTO_CMD_STORAGE_REPORT_STATUS     76  //report specified storage server status
#define TRACKER_PROTO_CMD_STORAGE_PARAMETER_REQ	    75  //storage server request parameters
#define TRACKER_PROTO_CMD_STORAGE_REPORT_TRUNK_FREE 74  //storage report trunk free space
#define TRACKER_PROTO_CMD_STORAGE_REPORT_TRUNK_FID  73  //storage report current trunk file id
#define TRACKER_PROTO_CMD_STORAGE_FETCH_TRUNK_FID   72  //storage get current trunk file id
#define TRACKER_PROTO_CMD_STORAGE_GET_STATUS	    71  //get storage status from tracker
#define TRACKER_PROTO_CMD_STORAGE_GET_SERVER_ID	    70  //get storage server id from tracker
#define TRACKER_PROTO_CMD_STORAGE_FETCH_STORAGE_IDS 69  //get all storage ids from tracker

#define TRACKER_PROTO_CMD_TRACKER_GET_SYS_FILES_START 61  //start of tracker get system data files
#define TRACKER_PROTO_CMD_TRACKER_GET_SYS_FILES_END   62  //end of tracker get system data files
#define TRACKER_PROTO_CMD_TRACKER_GET_ONE_SYS_FILE    63  //tracker get a system data file
#define TRACKER_PROTO_CMD_TRACKER_GET_STATUS          64  //tracker get status of other tracker
#define TRACKER_PROTO_CMD_TRACKER_PING_LEADER         65  //tracker ping leader
#define TRACKER_PROTO_CMD_TRACKER_NOTIFY_NEXT_LEADER  66  //notify next leader to other trackers
#define TRACKER_PROTO_CMD_TRACKER_COMMIT_NEXT_LEADER  67  //commit next leader to other trackers

#define TRACKER_PROTO_CMD_SERVER_LIST_ONE_GROUP			90
#define TRACKER_PROTO_CMD_SERVER_LIST_ALL_GROUPS		91
#define TRACKER_PROTO_CMD_SERVER_LIST_STORAGE			92
#define TRACKER_PROTO_CMD_SERVER_DELETE_STORAGE			93
#define TRACKER_PROTO_CMD_SERVER_SET_TRUNK_SERVER		94
#define TRACKER_PROTO_CMD_SERVICE_QUERY_STORE_WITHOUT_GROUP_ONE	101
#define TRACKER_PROTO_CMD_SERVICE_QUERY_FETCH_ONE		102
#define TRACKER_PROTO_CMD_SERVICE_QUERY_UPDATE  		103
#define TRACKER_PROTO_CMD_SERVICE_QUERY_STORE_WITH_GROUP_ONE	104
#define TRACKER_PROTO_CMD_SERVICE_QUERY_FETCH_ALL		105
#define TRACKER_PROTO_CMD_SERVICE_QUERY_STORE_WITHOUT_GROUP_ALL	106
#define TRACKER_PROTO_CMD_SERVICE_QUERY_STORE_WITH_GROUP_ALL	107
#define TRACKER_PROTO_CMD_RESP					100
#define FDFS_PROTO_CMD_ACTIVE_TEST				111  //active test, tracker and storage both support since V1.28

#define STORAGE_PROTO_CMD_REPORT_SERVER_ID	9  
#define STORAGE_PROTO_CMD_UPLOAD_FILE		11
#define STORAGE_PROTO_CMD_DELETE_FILE		12
#define STORAGE_PROTO_CMD_SET_METADATA		13
#define STORAGE_PROTO_CMD_DOWNLOAD_FILE		14
#define STORAGE_PROTO_CMD_GET_METADATA		15
#define STORAGE_PROTO_CMD_SYNC_CREATE_FILE	16
#define STORAGE_PROTO_CMD_SYNC_DELETE_FILE	17
#define STORAGE_PROTO_CMD_SYNC_UPDATE_FILE	18
#define STORAGE_PROTO_CMD_SYNC_CREATE_LINK	19
#define STORAGE_PROTO_CMD_CREATE_LINK		20
#define STORAGE_PROTO_CMD_UPLOAD_SLAVE_FILE	21
#define STORAGE_PROTO_CMD_QUERY_FILE_INFO	22
#define STORAGE_PROTO_CMD_UPLOAD_APPENDER_FILE	23   //create appender file
#define STORAGE_PROTO_CMD_APPEND_FILE		24		 //append file
#define STORAGE_PROTO_CMD_SYNC_APPEND_FILE	25
#define STORAGE_PROTO_CMD_FETCH_ONE_PATH_BINLOG	26   //fetch binlog of one store path
#define STORAGE_PROTO_CMD_RESP			TRACKER_PROTO_CMD_RESP
#define STORAGE_PROTO_CMD_UPLOAD_MASTER_FILE	STORAGE_PROTO_CMD_UPLOAD_FILE

#define STORAGE_PROTO_CMD_TRUNK_ALLOC_SPACE   	     27  //since V3.00, storage to trunk server
#define STORAGE_PROTO_CMD_TRUNK_ALLOC_CONFIRM	     28  //since V3.00, storage to trunk server
#define STORAGE_PROTO_CMD_TRUNK_FREE_SPACE	         29  //since V3.00, storage to trunk server
#define STORAGE_PROTO_CMD_TRUNK_SYNC_BINLOG	         30  //since V3.00, trunk storage to storage
#define STORAGE_PROTO_CMD_TRUNK_GET_BINLOG_SIZE	     31  //since V3.07, tracker to storage
#define STORAGE_PROTO_CMD_TRUNK_DELETE_BINLOG_MARKS  32  //since V3.07, tracker to storage
#define STORAGE_PROTO_CMD_TRUNK_TRUNCATE_BINLOG_FILE 33  //since V3.07, trunk storage to storage

#define STORAGE_PROTO_CMD_MODIFY_FILE		     34  //since V3.08
#define STORAGE_PROTO_CMD_SYNC_MODIFY_FILE	     35  //since V3.08
#define STORAGE_PROTO_CMD_TRUNCATE_FILE		     36  //since V3.08
#define STORAGE_PROTO_CMD_SYNC_TRUNCATE_FILE	 37  //since V3.08

//for overwrite all old metadata
#define STORAGE_SET_METADATA_FLAG_OVERWRITE		'O'
#define STORAGE_SET_METADATA_FLAG_OVERWRITE_STR	"O"
//for replace, insert when the meta item not exist, otherwise update it
#define STORAGE_SET_METADATA_FLAG_MERGE		'M'
#define STORAGE_SET_METADATA_FLAG_MERGE_STR	"M"

#define IP_ADDRESS_SIZE				16

#define FDFS_VERSION_SIZE			6
#define FDFS_GROUP_NAME_MAX_LEN		16
#define FDFS_STORAGE_ID_MAX_SIZE	16
#define FDFS_DOMAIN_NAME_MAX_SIZE	128
#define FDFS_REMOTE_NAME_MAX_SIZE	128

#define FDFS_FILE_ID_SEPERATOR		'/'
#define FDFS_FILE_EXT_NAME_MAX_LEN	6

#define FDFS_PROTO_PKG_LEN_SIZE		8
#define FDFS_PROTO_CMD_SIZE			1
#define FDFS_PROTO_IP_PORT_SIZE		(IP_ADDRESS_SIZE + 6)

#define TRACKER_QUERY_STORAGE_FETCH_BODY_LEN     (FDFS_GROUP_NAME_MAX_LEN + IP_ADDRESS_SIZE - 1 + FDFS_PROTO_PKG_LEN_SIZE)
#define TRACKER_QUERY_STORAGE_STORE_BODY_LEN     (FDFS_GROUP_NAME_MAX_LEN + IP_ADDRESS_SIZE - 1 + FDFS_PROTO_PKG_LEN_SIZE + 1)
//#define STORAGE_TRUNK_ALLOC_CONFIRM_REQ_BODY_LEN (FDFS_GROUP_NAME_MAX_LEN + sizeof(FDFSTrunkInfoBuff))

typedef struct {
	char pkg_len[FDFS_PROTO_PKG_LEN_SIZE];  //body length, not including header
	char cmd;    //command code
	char status; //status code for response
} TrackerHeader;

typedef struct {
	char group_name[FDFS_GROUP_NAME_MAX_LEN]; // 组名称...
	char ip_addr[IP_ADDRESS_SIZE]; // 组IP地址...
	int  store_path_index; // 路径编号...
	int  port; // 端口...
} StorageServer;

typedef struct {
	char group_name[FDFS_GROUP_NAME_MAX_LEN + 1];
	char sz_total_mb[FDFS_PROTO_PKG_LEN_SIZE]; //total disk storage in MB
	char sz_free_mb[FDFS_PROTO_PKG_LEN_SIZE];  //free disk storage in MB
	char sz_trunk_free_mb[FDFS_PROTO_PKG_LEN_SIZE];  //trunk free space in MB
	char sz_count[FDFS_PROTO_PKG_LEN_SIZE];    //server count
	char sz_storage_port[FDFS_PROTO_PKG_LEN_SIZE];
	char sz_storage_http_port[FDFS_PROTO_PKG_LEN_SIZE];
	char sz_active_count[FDFS_PROTO_PKG_LEN_SIZE]; //active server count
	char sz_current_write_server[FDFS_PROTO_PKG_LEN_SIZE];
	char sz_store_path_count[FDFS_PROTO_PKG_LEN_SIZE];
	char sz_subdir_count_per_path[FDFS_PROTO_PKG_LEN_SIZE];
	char sz_current_trunk_file_id[FDFS_PROTO_PKG_LEN_SIZE];
} TrackerGroupStat;

typedef struct
{
	char group_name[FDFS_GROUP_NAME_MAX_LEN + 8];  //for 8 bytes alignment
	int64_t total_mb;  //total disk storage in MB
	int64_t free_mb;  //free disk storage in MB
	int64_t trunk_free_mb;  //trunk free disk storage in MB
	int count;        //server count
	int storage_port; //storage server port
	int storage_http_port; //storage server http port
	int active_count; //active server count
	int current_write_server; //current server index to upload file
	int store_path_count;  //store base path count of each storage server
	int subdir_count_per_path;
	int current_trunk_file_id;  //current trunk file id
} FDFSGroupStat;
