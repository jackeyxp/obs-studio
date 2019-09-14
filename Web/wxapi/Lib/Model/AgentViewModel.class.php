<?php

class AgentViewModel extends ViewModel {
    public $viewFields = array(
        'Agent'=>array('*','_type'=>'LEFT'), //Must be LEFT 返回左表所有数据，即使右表没有匹配的
        'User'=>array('user_id','real_name','wx_nickname','_on'=>'Agent.master_id=User.user_id'),
    );
}
?>