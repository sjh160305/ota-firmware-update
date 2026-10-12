.PHONY: clean All Project_Title Project_PreBuild chip_td1601 console csi minilibc mm td1601_evb Project_Build Project_PostBuild

All: Project_Title Project_PreBuild chip_td1601 console csi minilibc mm td1601_evb Project_Build Project_PostBuild

Project_Title:
	@echo "----------Building project:[ hello_world - BuildSet ]----------"

Project_PreBuild:
	@echo Executing Pre Build commands ...
	@export BOARD_PATH="C:/Users/006143/Desktop/A_test/Ad1602/Ad1601/Ad1601_cdk/boards/td1601_evb/v1.0" CDKPath="D:/C-Sky/CDK" CDK_VERSION="V2.22.0" CHIP_PATH="C:/Users/006143/Desktop/A_test/Ad1602/Ad1601/Ad1601_cdk/components/chips/chip_td1601/v1.0" ProjectName="hello_world" ProjectPath="C:/Users/006143/Desktop/A_test/Ad1602/Ad1601/Ad1601_cdk/projects/app/" SOLUTION_PATH="C:/Users/006143/Desktop/A_test/Ad1602/Ad1601/Ad1601_cdk/projects/app/" chip_td1601="V1.0.0" console="V1.0.0" csi="V1.0.0" minilibc="V1.0.0" mm="V1.0.0" td1601_evb="V1.0.0" && "C:/Users/006143/Desktop/A_test/Ad1602/Ad1601/Ad1601_cdk/projects/app/utilities/pre_build.sh"
	@echo Done

chip_td1601:
	@make -r -f Obj/BuildSet/Packages/chip_td1601/V1.0.0/Makefile -j 16 -C  ./ 

console:
	@make -r -f Obj/BuildSet/Packages/console/V1.0.0/Makefile -j 16 -C  ./ 

csi:
	@make -r -f Obj/BuildSet/Packages/csi/V1.0.0/Makefile -j 16 -C  ./ 

minilibc:
	@make -r -f Obj/BuildSet/Packages/minilibc/V1.0.0/Makefile -j 16 -C  ./ 

mm:
	@make -r -f Obj/BuildSet/Packages/mm/V1.0.0/Makefile -j 16 -C  ./ 

td1601_evb:
	@make -r -f Obj/BuildSet/Packages/td1601_evb/V1.0.0/Makefile -j 16 -C  ./ 


Project_Build:
	@make -r -f hello_world.mk -j 16 -C  ./ 

Project_PostBuild:
	@echo Executing Post Build commands ...
	@export BOARD_PATH="C:/Users/006143/Desktop/A_test/Ad1602/Ad1601/Ad1601_cdk/boards/td1601_evb/v1.0" CDKPath="D:/C-Sky/CDK" CDK_VERSION="V2.22.0" CHIP_PATH="C:/Users/006143/Desktop/A_test/Ad1602/Ad1601/Ad1601_cdk/components/chips/chip_td1601/v1.0" ProjectName="hello_world" ProjectPath="C:/Users/006143/Desktop/A_test/Ad1602/Ad1601/Ad1601_cdk/projects/app/" SOLUTION_PATH="C:/Users/006143/Desktop/A_test/Ad1602/Ad1601/Ad1601_cdk/projects/app/" chip_td1601="V1.0.0" console="V1.0.0" csi="V1.0.0" minilibc="V1.0.0" mm="V1.0.0" td1601_evb="V1.0.0" && "C:/Users/006143/Desktop/A_test/Ad1602/Ad1601/Ad1601_cdk/projects/app/utilities/aft_build.sh"
	@echo Done


clean:
	@echo "----------Cleaning project:[ hello_world - BuildSet ]----------"

