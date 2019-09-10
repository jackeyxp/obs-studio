<?php
/*************************************************************
    Wan (C)2016- 2017 myhaoyi.com
    备注：专门处理电脑端强求页面...
*************************************************************/

class IndexAction extends Action
{
  public function _initialize() {
  }
  /**
  +----------------------------------------------------------
  * 默认操作 - 处理全部非微信端的访问...
  +----------------------------------------------------------
  */
  public function index()
  {
    $this->assign('my_ver', C('VERSION'));
    $this->display('index');
  }
  //
  // 更新日志 页面...
  public function changelog()
  {
    $this->display('changelog');
  }
}
?>