<?php
/*************************************************************
    Wan (C)2015 - 2016 happyhope.net
*************************************************************/

// 强制默认编码...
header('Content-Type: text/html; charset=utf-8');

// 特殊标志?
define('IN_WK', TRUE);

// 定义统一的token
define('WK_TOKEN', 'weixin');

// 报错等级...
error_reporting(7);

// 定义当前目录为根目录...
define('WK_ROOT', dirname(__FILE__));

// 定义框架路径...
define('THINK_PATH','./ThinkPHP');
// 定义前台页面...
define('APP_NAME', 'wxapi');
define('APP_PATH', './wxapi');
// 定义前台风格...
define('DEFAULT_TYPE','default');
// 定义公共路径位置...
define('__PUBLIC__',"wxapi/public");

// 加载框架和公共函数...
require(THINK_PATH.'/ThinkPHP.php');

// 加载预定义的常量...
require(APP_PATH."/Common/errCode.php");

// 加载浏览器检测工具...
//require(APP_PATH.'/Common/Mobile_Detect.php');

// 加载微信调用接口...
//require(APP_PATH.'/Common/wechat.class.php');

// 正常运行...
App::run();

?>
