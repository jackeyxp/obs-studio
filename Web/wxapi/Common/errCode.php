<?php
/*************************************************************
    HaoYi (C)2017 - 2020 myhaoyi.com
*************************************************************/

// 定义日志存储函数
function logdebug($text)
{
  $strValue = sprintf("%s\t%s\n", date('Y-m-d H:i:s'), $text);
  file_put_contents(WK_ROOT . '/logwechat.txt', $strValue, FILE_APPEND);
}

define('LIVE_BEGIN_ID',  10000);   // 直播间开始编号...

// 定义性别类型标识符号...
define('kNoneSex',           0);
define('kMaleSex',           1);
define('kFeMaleSex',         2);
// 定义性别名称数组，需要使用eval才能返回数组...
define('SEX_TYPE', "return array('无', '男', '女');");

// 定义用户类型标识符号...
define('kParentUser',        0);    // 家长
define('kAssistUser',        1);    // 助教
define('kTeacherUser',       2);    // 讲师
define('kShopMasterUser',    3);    // 学校|门店管理员
define('kShopOwnerUser',     4);    // 学校|门店拥有者
define('kMaintainUser',      5);    // 运营维护者
define('kAdministerUser',    6);    // 系统管理员
// 定义用户类型名称数组，需要使用eval才能返回数组...
define('USER_TYPE', "return array('家长', '助教', '讲师', '管理员', '拥有者', '运营维护者', '系统管理员');");

// 定义父母类型标识符号...
define('kNoneParent',        0);     // 无
define('kMamaParent',        1);     // 妈妈
define('kPapaParent',        2);     // 爸爸
define('kFamiParent',        3);     // 亲属
// 定义父母类型名称数组，需要使用eval才能返回数组...
define('PARENT_TYPE', "return array('无', '妈妈', '爸爸', '亲属');");

// 定义支付方式
define('PAY_WECHAT',                0);       // 0,微信支付
define('PAY_CASH',                  1);       // 1,现金支付

// 定义用户消费状态
define('STATUS_WAIT_PAY',           0);       // 0,商品已放购物车，等待买家付款，这时商家可修改单价
define('STATUS_HAVE_PAY',           1);       // 1,商品已完成付款。

// 定义支付错误号
define('PAY_ERR_SUCCESS',           0);       // 0,支付成功
define('PAY_ERR_RETURN',            1);       // 1,通信出错,return_code
define('PAY_ERR_RESULT',            2);       // 2,业务出错,result_code

// 定义用户消费类型
define('MODE_BUY_CLASS',            0);       // 0,购买课程
define('MODE_BUY_MEMBER',           1);       // 1,购买会员

// 定义UDP中转服务器支持的客户端类型...
define('kClientPHP',          1);
define('kClientStudent',      2);
define('kClientTeacher',      3);
define('kClientUdpServer',    4);
define('kClientScreen',       5);

// 定义绑定登录子命令...
define('BIND_SCAN',           1);
define('BIND_SAVE',           2);
define('BIND_CANCEL',         3);

// 定义UDP服务器反馈错误号...
define('ERR_OK',                      0);
define('ERR_NO_ROOM',             10001);
define('ERR_NO_SERVER',           10002);
define('ERR_MODE_MATCH',          10003);
define('ERR_NO_PARAM',            10004);
define('ERR_NO_TERMINAL',         10005);
define('ERR_TYPE_MATCH',          10006);
define('ERR_TIME_MATCH',          10007);
define('ERR_HAS_TEACHER',         10008);

// 定义UDP服务器可以执行的命令列表 => 从1开始编号...
define('CMD_LINE_START',              __LINE__);
define('kCmd_Smart_Login',            __LINE__ - CMD_LINE_START);
define('kCmd_Smart_OnLine',           __LINE__ - CMD_LINE_START);
define('kCmd_Live_OnLine',            __LINE__ - CMD_LINE_START);
define('kCmd_UDP_Logout',             __LINE__ - CMD_LINE_START);
define('kCmd_UdpServer_Login',        __LINE__ - CMD_LINE_START);
define('kCmd_UdpServer_OnLine',       __LINE__ - CMD_LINE_START);
define('kCmd_UdpServer_AddTeacher',   __LINE__ - CMD_LINE_START);
define('kCmd_UdpServer_DelTeacher',   __LINE__ - CMD_LINE_START);
define('kCmd_UdpServer_AddStudent',   __LINE__ - CMD_LINE_START);
define('kCmd_UdpServer_DelStudent',   __LINE__ - CMD_LINE_START);
define('kCmd_PHP_GetUdpServer',       __LINE__ - CMD_LINE_START);
define('kCmd_PHP_GetAllServer',       __LINE__ - CMD_LINE_START);
define('kCmd_PHP_GetAllClient',       __LINE__ - CMD_LINE_START);
define('kCmd_PHP_GetRoomList',        __LINE__ - CMD_LINE_START);
define('kCmd_PHP_GetPlayerList',      __LINE__ - CMD_LINE_START);
define('kCmd_PHP_Bind_Mini',          __LINE__ - CMD_LINE_START);
define('kCmd_PHP_GetRoomFlow',        __LINE__ - CMD_LINE_START);
define('kCmd_Screen_Login',           __LINE__ - CMD_LINE_START);
define('kCmd_Screen_OnLine',          __LINE__ - CMD_LINE_START);
define('kCmd_Screen_Packet',          __LINE__ - CMD_LINE_START);
define('kCmd_Screen_Finish',          __LINE__ - CMD_LINE_START);

//////////////////////////////////////////////////////
// 定义一组通用的公用函数列表...
//////////////////////////////////////////////////////

function userTextEncode($str) {
  if (!is_string($str)) return $str;
  if (!$str || $str=='undefined') return '';
  $text = json_encode($str); //暴露出unicode
  $text = preg_replace_callback("/(\\\u[ed][0-9a-f]{3})/i", function($str){ return addslashes($str[0]); }, $text); //将emoji的unicode留下，其他不动，这里的正则比原答案增加了d，因为我发现我很多emoji实际上是\ud开头的，反而暂时没发现有\ue开头。
  return json_decode($text);
}

function userTextDecode($str) {
  $text = json_encode($str); //暴露出unicode
  $text = preg_replace_callback('/\\\\\\\\/i', function($str) { return '\\'; }, $text); //将两条斜杠变成一条，其他不动
  return json_decode($text);
}

//
// 去除昵称里的emoji表情符号信息...
function trimEmo($inNickName)
{
  // 对微信昵称进行json编码...
  $strName = json_encode($inNickName);
  // 将emoji的unicode置为空，其他不动...
  $strName = preg_replace("#(\\\ud[0-9a-f]{3})|(\\\ue[0-9a-f]{3})#ie", "", $strName);
  $strName = json_decode($strName);
  return $strName;
}
//
// 去掉最后一个字符，如果是反斜杠...
function removeSlash($strUrl)
{
  $nSize = strlen($strUrl) - 1;
  // 去掉最后的反斜杠字符...
  if( $strUrl{$nSize} == '/' ) {
    $strUrl = substr($strUrl, 0, $nSize);
  }
  return $strUrl;
}
//
// base64编码处理...
function urlsafe_b64encode($string)
{
  $data = base64_encode($string);
  $data = str_replace(array('+','/','='),array('-','_',''),$data);
  return $data;
}
//
// base64解码处理...
function urlsafe_b64decode($string)
{
  $data = str_replace(array('-','_'),array('+','/'),$string);
  $mod4 = strlen($data) % 4;
  if($mod4) {
    $data .= substr('====', $mod4);
  }
  return base64_decode($data);
}  
/**
* GET 请求
* @param string $url
*/
function http_get( $url )
{
  $oCurl = curl_init();
  if(stripos($url,"https://")!==FALSE) {
    curl_setopt($oCurl, CURLOPT_SSL_VERIFYPEER, FALSE);
    curl_setopt($oCurl, CURLOPT_SSL_VERIFYHOST, FALSE);
    curl_setopt($oCurl, CURLOPT_SSLVERSION, 1); //CURL_SSLVERSION_TLSv1
  }
  curl_setopt($ch, CURLOPT_TIMEOUT, 3);
  curl_setopt($oCurl, CURLOPT_URL, $url);
  curl_setopt($oCurl, CURLOPT_RETURNTRANSFER, 1 );
	// 2017.06.06 => resolve DNS slow problem...
	curl_setopt($oCurl, CURLOPT_HTTPHEADER, array("Expect: ")); 
	curl_setopt($oCurl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4 );
	curl_setopt($oCurl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);
  $sContent = curl_exec($oCurl);
  $aStatus = curl_getinfo($oCurl);
  curl_close($oCurl);
  if(intval($aStatus["http_code"])==200){
    return $sContent;
  }else{
    return false;
  }
}
/**
 * POST 请求
 * @param string $url
 * @param array $param
 * @param boolean $post_file 是否文件上传
 * @return string content
 */
function http_post($url,$param,$post_file=false)
{
	$oCurl = curl_init();
	if(stripos($url,"https://")!==FALSE){
		curl_setopt($oCurl, CURLOPT_SSL_VERIFYPEER, FALSE);
		curl_setopt($oCurl, CURLOPT_SSL_VERIFYHOST, false);
		curl_setopt($oCurl, CURLOPT_SSLVERSION, 1); //CURL_SSLVERSION_TLSv1
	}
	if (is_string($param) || $post_file) {
		$strPOST = $param;
	} else {
		$aPOST = array();
		foreach($param as $key=>$val){
			$aPOST[] = $key."=".urlencode($val);
		}
		$strPOST =  join("&", $aPOST);
	}
	curl_setopt($oCurl, CURLOPT_URL, $url);
	curl_setopt($oCurl, CURLOPT_RETURNTRANSFER, 1 );
	curl_setopt($oCurl, CURLOPT_POST,true);
	curl_setopt($oCurl, CURLOPT_POSTFIELDS,$strPOST);
	$sContent = curl_exec($oCurl);
	$aStatus = curl_getinfo($oCurl);
	curl_close($oCurl);
	if(intval($aStatus["http_code"])==200){
		return $sContent;
	}else{
		return false;
	}
}
/**
* 作用：产生随机字符串，不长于32位
*/
// 生成一个不超过32位的state...
//$state = $this->createNoncestr();
function createNoncestr( $length = 32 ) 
{
  $chars = "abcdefghijklmnopqrstuvwxyz0123456789";  
  $str ="";
  for ( $i = 0; $i < $length; $i++ )  {  
    $str.= substr($chars, mt_rand(0, strlen($chars)-1), 1);  
  }
  return $str;
}

// 获取转发错误信息...
function getTransmitErrMsg($inErrCode)
{
  switch( $inErrCode )
  {
    case ERR_OK:          $strErrMsg = 'ok'; break;
    case ERR_NO_ROOM:     $strErrMsg = '没有房间号。'; break;
    case ERR_NO_SERVER:   $strErrMsg = '没有在线的直播服务器。'; break;
    case ERR_MODE_MATCH:  $strErrMsg = '终端运行模式与直播服务器运行模式不匹配。'; break;
    case ERR_NO_PARAM:    $strErrMsg = '参数错误或无效。'; break;
    case ERR_NO_TERMINAL: $strErrMsg = '没有找到指定的终端对象。'; break;
    case ERR_TYPE_MATCH:  $strErrMsg = '终端类型不匹配。'; break;
    case ERR_TIME_MATCH:  $strErrMsg = '时间戳标识不匹配。'; break;
    case ERR_HAS_TEACHER: $strErrMsg = '同一个教室，只能登录一个讲师端，请选择其它教室登录。'; break;
    default:              $strErrMsg = '未知错误，请确认中心服务器版本。'; break;
  }
  return $strErrMsg;
}

// 代替扩展插件的，直接用socket实现的与中转服务器通信函数...
/*function php_transmit_command($inIPAddr, $inIPPort, $inClientType, $inCmdID, $inSaveJson = false)
{
  // 创建并连接服务器...
  $socket = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
  if( !$socket ) return false;
  // 链接指定的服务器和端口...
  if( !socket_connect($socket, $inIPAddr, $inIPPort) ) {
    socket_close($socket);
    return false;
  }
  // 组合发送数据包 => m_pkg_len | m_type | m_cmd | m_sock
  $pkg_len = (($inSaveJson) ? strlen($inSaveJson) : 0);
  $strData = pack('LLLL', $pkg_len, $inClientType, $inCmdID, 0);
  // 计算带有数据包的缓存内容...
  $strData = (($inSaveJson) ? ($strData . $inSaveJson) : $strData);
  // 发送已经准备好的数据包...
  if( !socket_write($socket, $strData, strlen($strData)) ) {
    socket_close($socket);
    return false;
  }
  // 接收服务器传递过来的数据包...
  $strRecv = socket_read($socket, 8192);
  if( !$strRecv ) {
    socket_close($socket);
    return false;
  }
  // 去掉10字节的数据包...
  if( strlen($strRecv) > 10 ) {
    $strJson = substr($strRecv, 10, strlen($strRecv) - 10);
  }
  // 关闭连接，返回json数据包..
  socket_close($socket);
  return $strJson;
}*/

?>