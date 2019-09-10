更新步骤如下：
====================================================
1. 修改 F:\obs-studio\libobs\obs-config.h 里面的版本号码；
2. 重新编译 Smart 都需要完整编译发行版本；
3. obs.dll编译时已经自动投递到Smart发行目录；
4. 编写更新日志：
   F:\obs-studio\vsbuild\update_studio\smart\changelog.txt => Smart更新日志
5. 双击 json_smart.bat 生成如下脚本：
   json_smart.bat => F:\obs-studio\vsbuild\update_studio\smart\manifest.json
6. 将生成的 manifest.json 更新到升级服务器对应的目录下面；
7. 将编译好的 Smart 二进制文件上传到升级服务器对应的目录下面；
8. 通过github把新的变更上传到代码管理服务器当中；
9. 转移到 E:\GitHub\HaoYiYun\Install 目录进行打包；
10. 将打包后的 Smart 上传到网站的下载目录，修改网站文字和连接；
