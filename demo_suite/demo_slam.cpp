/**
 * \file demo_slam.cpp
 *
 * ## Add brief description here ##
 *
 * \author jsola@laas.fr
 * \date 28/04/2010
 *
 *  ## Add a description here ##
 *
 * \ingroup rtslam
 */


#include <iostream>
#include <boost/shared_ptr.hpp>
#include <time.h>
#include <map>

// jafar debug include
#include "kernel/jafarDebug.hpp"
#include "kernel/jafarTestMacro.hpp"
#include "kernel/timingTools.hpp"
#include "jmath/random.hpp"
#include "jmath/matlab.hpp"

#include "jmath/ublasExtra.hpp"

#include "rtslam/rtSlam.hpp"
//#include "rtslam/robotOdometry.hpp"
#include "rtslam/robotConstantVelocity.hpp"
//#include "rtslam/robotInertial.hpp"
#include "rtslam/sensorPinHole.hpp"
#include "rtslam/landmarkAnchoredHomogeneousPoint.hpp"
//#include "rtslam/landmarkEuclideanPoint.hpp"
#include "rtslam/observationFactory.hpp"
#include "rtslam/activeSearch.hpp"
#include "rtslam/featureAbstract.hpp"
#include "rtslam/rawImage.hpp"
#include "rtslam/descriptorImagePoint.hpp"

#include "rtslam/hardwareSensorCameraFirewire.hpp"

#include "rtslam/display_qt.hpp"

using namespace jblas;
using namespace jafar;
using namespace jafar::jmath;
using namespace jafar::jmath::ublasExtra;
using namespace jafar::rtslam;
using namespace boost;

int mode = 0;
std::string dump_path = ".";


void demo_slam01_main(world_ptr_t *world) {

	const int    	N_FRAMES    = 5000;
	const int    	MAP_SIZE    = 250;
	const int    	N_UPDATES   = 15;
	const double 	FRAME_RATE  = 60;

	const int    	PATCH_SIZE  = 7;
	const int			DESC_SIZE		= PATCH_SIZE*3;
	const int 		GRID_UCELLS	= 3;
	const int			GRID_VCELLS	= 3;
	const int			GRID_MARGIN	= (DESC_SIZE-1)/2;
	const int			GRID_SEPAR	= PATCH_SIZE;

	const double	PERT_VLIN		= 4.0; // m/s per sqrt(s)
	const double	PERT_VANG		= 1.0; // rad/s per sqrt(s)

	const bool		SHOW_PATCH	= true;


	time_t rseed = time(NULL);
	if (mode == 1)
	{
		std::fstream f((dump_path + std::string("/rseed.log")).c_str(), std::ios_base::out);
		f << rseed << std::endl;
		f.close();
	} else if (mode == 2)
	{
		std::fstream f((dump_path + std::string("/rseed.log")).c_str(), std::ios_base::in);
		f >> rseed;
		f.close();
	}
	std::cout << "rseed " << rseed << std::endl;
	srand(rseed);

	int imgWidth = 640, imgHeight = 480;
	double _d[3] = {-0.27572, 0.28827}; //{-0.27965, 0.20059, -0.14215}; //{-0.27572, 0.28827};
//	double _d[0];
	vec d = createVector<3>(_d);
	double _k[4] = {301.60376, 266.29546, 519.67406, 519.54656};
//	double _k[4] = {320, 240, 320, 320};
	vec k = createVector<4>(_k);
	ActiveSearchGrid asGrid(imgWidth, imgHeight, GRID_UCELLS, GRID_VCELLS, GRID_MARGIN, GRID_SEPAR);

	ObservationFactory obsFact;
	obsFact.addMaker(boost::shared_ptr<ObservationMakerAbstract>(new PinholeEucpObservationMaker()));
	obsFact.addMaker(boost::shared_ptr<ObservationMakerAbstract>(new PinholeAhpObservationMaker()));

	// INIT : map, rob, sen
	//world_ptr_t worldPtr(new WorldAbstract());
	world_ptr_t worldPtr = *world;
	worldPtr->display_mutex.lock();

	// create map
	map_ptr_t mapPtr(new MapAbstract(MAP_SIZE));
	worldPtr->addMap(mapPtr);
	mapPtr->clear();

	// create robots
	robconstvel_ptr_t robPtr1(new RobotConstantVelocity(mapPtr));
	robPtr1->setId();
	robPtr1->linkToParentMap(mapPtr);
	robPtr1->state.clear();
	robPtr1->pose.x(quaternion::originFrame());
	robPtr1->dt_or_dx = 1/FRAME_RATE;
	double _v[6] = {PERT_VLIN,PERT_VLIN,PERT_VLIN,PERT_VANG,PERT_VANG,PERT_VANG};
	robPtr1->perturbation.clear();
	robPtr1->perturbation.set_std_continuous(createVector<6>(_v));
	robPtr1->constantPerturbation = false;

	// create sensors
	pinhole_ptr_t senPtr11 (new SensorPinHole(robPtr1, MapObject::UNFILTERED));
	senPtr11->setId();
	senPtr11->linkToParentRobot(robPtr1);
	senPtr11->state.clear();
	senPtr11->pose.x(quaternion::originFrame());
	senPtr11->params.setImgSize(imgWidth, imgHeight);
	senPtr11->params.setIntrinsicCalibration(k, d, d.size());
	senPtr11->params.setMiscellaneous(1.0, 0.1, PATCH_SIZE);

	viam_hwmode_t hwmode = { VIAM_HWSZ_640x480, VIAM_HWFMT_MONO8, VIAM_HW_FIXED, VIAM_HWFPS_60, VIAM_HWTRIGGER_INTERNAL };
	// UNCOMMENT THESE TWO LINES TO ENABLE FIREWIRE CAMERA OPERATION
	hardware_sensor_ptr_t hardSen11(new HardwareSensorCameraFirewire("0x00b09d01006fb38f", hwmode, mode, dump_path));
	senPtr11->setHardwareSensor(hardSen11);

	// Show empty map
	cout << *mapPtr << endl;

	worldPtr->display_mutex.unlock();


	// INIT : complete observations set
	// loop all sensors
	// loop all lmks
	// create sen--lmk observation
	// Temporal loop

	kernel::Chrono chrono;
	kernel::Chrono total_chrono;
	kernel::Chrono mutex_chrono;
	double max_dt = 0;
	for (int t = 1; t <= N_FRAMES;) {
//		sleep(1);
		bool had_data = false;

		worldPtr->display_mutex.lock();
//		cout << "\n************************************************** " << endl;
//		cout << "\n                 FRAME : " << t << " (blocked " << mutex_chrono.elapsedMicrosecond() << " us)" << endl;
		chrono.reset();
		int total_match_time = 0;
		int total_update_time = 0;

		// foreach robot
		for (MapAbstract::RobotList::iterator robIter = mapPtr->robotList().begin(); robIter != mapPtr->robotList().end(); robIter++)
		{
			robot_ptr_t robPtr = *robIter;

//			cout << "\n================================================== " << endl;
//			cout << *robPtr << endl;

			// foreach sensor
//kernel::Chrono chronototal;
			for (RobotAbstract::SensorList::iterator senIter = robPtr->sensorList().begin(); senIter != robPtr->sensorList().end(); senIter++)
			{
				sensor_ptr_t senPtr = *senIter;
//				cout << "\n________________________________________________ " << endl;
//				cout << *senPtr << endl;

				// get raw-data
				if (senPtr->acquireRaw() < 0) continue;
//std::cout << chronototal.elapsed() << " has acquired" << std::endl;

				// move the filter time to the data raw
				vec u(robPtr->mySize_control()); // TODO put some real values in u.
				fillVector(u, 0.0);
				// robPtr->set_control(u);
				robPtr->move(u, senPtr->getRaw()->timestamp);

				asGrid.renew();

				// 1. Observe known landmarks
				// foreach observation
				int numObs = 0;
				observation_ptr_t obsPtr;

				// the list of observations sorted by information gain
				map<double, observation_ptr_t> obsSortedList;


				// loop all observations
				for (SensorAbstract::ObservationList::iterator obsIter = senPtr->observationList().begin(); obsIter != senPtr->observationList().end(); obsIter++)
				{
					obsPtr = *obsIter;

					obsPtr->clearEvents();
					obsPtr->measurement.matchScore = 0;

					// 1a. project
					obsPtr->project();

						// 1b. check visibility
					obsPtr->predictVisibility();

					if (obsPtr->isVisible()) {

						// Add to tesselation grid for active search
						asGrid.addPixel(obsPtr->expectation.x());

						// predict information gain
						obsPtr->predictInfoGain();

						// add to sorted list
						obsSortedList[obsPtr->expectation.infoGain] = obsPtr;
					} // visible obs
				} // for each obs



				// loop only the N_UPDATES most interesting obs, from largest info gain to smallest
				for (map<double, observation_ptr_t>::reverse_iterator obsIter = obsSortedList.rbegin(); obsIter != obsSortedList.rend(); obsIter++) {
					obsPtr = obsIter->second;

					// 1a. project
					obsPtr->project();

						// 1b. check visibility
					obsPtr->predictVisibility();

					if (obsPtr->isVisible()) {

//cout << "info gain " << obsPtr->expectation.infoGain << endl;

						if (numObs < N_UPDATES) {

						// update counter
						obsPtr->counters.nSearch++;

						// 1c. predict appearance
						obsPtr->predictAppearance();

//						senPtr->dataManager.match(rawPtr, obsPtr);

						// 1d. search appearence in raw
						int xmin, xmax, ymin, ymax;
						double dx, dy;
						dx = 3.0*sqrt(obsPtr->expectation.P(0,0)+1.0);
						dy = 3.0*sqrt(obsPtr->expectation.P(1,1)+1.0);
						xmin = (int)(obsPtr->expectation.x(0)-dx);
						xmax = (int)(obsPtr->expectation.x(0)+dx+0.9999);
						ymin = (int)(obsPtr->expectation.x(1)-dy);
						ymax = (int)(obsPtr->expectation.x(1)+dy+0.9999);

						cv::Rect roi(xmin,ymin,xmax-xmin+1,ymax-ymin+1);



						kernel::Chrono match_chrono;
						senPtr->getRaw()->match(RawAbstract::ZNCC, obsPtr->predictedAppearance, roi, obsPtr->measurement, obsPtr->observedAppearance);
						total_match_time += match_chrono.elapsedMicrosecond();

						/*
  					 // DEBUG: save some appearances to file
						 ((AppearanceImagePoint*)(((DescriptorImagePoint*)(obsPtr->landmark().descriptorPtr.get()))->featImgPntPtr->appearancePtr.get()))->patch.save("descriptor_app.png");
						 ((AppearanceImagePoint*)(obsPtr->predictedAppearance.get()))->patch.save("predicted_app.png");
						 ((AppearanceImagePoint*)(obsPtr->observedAppearance.get()))->patch.save("matched_app.png");
            */
						// DEBUG: display predicted appearances on image, disable it when operating normally because can have side effects
						if (SHOW_PATCH) {
							AppearanceImagePoint
							* appImgPtr =
									PTR_CAST<AppearanceImagePoint*> (obsPtr->predictedAppearance.get());
							jblas::veci shift(2);
							shift(0) = (appImgPtr->patch.width() - 1) / 2;
							shift(1) = (appImgPtr->patch.height() - 1) / 2;
							appImgPtr->patch.robustCopy(*PTR_CAST<RawImage*> (senPtr->getRaw().get())->img, 0, 0, obsPtr->expectation.x(0) - shift(0), obsPtr->expectation.x(1) - shift(1));
						}

						// 1e. if feature is found
						if (obsPtr->getMatchScore()>0.90) {
							obsPtr->counters.nMatch++;
							obsPtr->events.matched = true;
							obsPtr->computeInnovation() ;

							// 1f. if feature is inlier
							if (obsPtr->compatibilityTest(3.0)) { // use 3.0 for 3-sigma or the 5% proba from the chi-square tables.
								numObs++;
								obsPtr->counters.nInlier++;
								kernel::Chrono update_chrono;
								obsPtr->update() ;
								total_update_time += update_chrono.elapsedMicrosecond();
								obsPtr->events.updated = true;
							} // obsPtr->compatibilityTest(3.0)

						} // obsPtr->getScoreMatchInPercent()>80

						} // number of observations

					} // obsPtr->isVisible()

					// cout << "\n-------------------------------------------------- " << endl;
					// cout << *obsPtr << endl;

				} // foreach observation



				// 2. init new landmarks
				#if 0
				for (LandmarkFactoryList::iterator it = senPtr->lmkFactories.begin(); it != senPtr->lmkFactories.end(); ++it)
				{
					if (mapPtr->unusedStates(it->size()))
					{

					}
				}
				#endif


				if (mapPtr->unusedStates(LandmarkAnchoredHomogeneousPoint::size())) {

					ROI roi;
					if (asGrid.getROI(roi)){

						feat_img_pnt_ptr_t featPtr(new FeatureImagePoint(PATCH_SIZE*3,PATCH_SIZE*3,CV_8U));
						if (senPtr->getRaw()->detect(RawAbstract::HARRIS, featPtr, &roi)) {
//							cout << "\n-------------------------------------------------- " << endl;
//							cout << "Detected pixel: " << featPtr->measurement.x() << endl;

//((AppearanceImagePoint*)(featPtr->appearancePtr.get()))->patch.save("detected_patch.png");

							// 2a. create lmk object
							ahp_ptr_t lmkPtr(new LandmarkAnchoredHomogeneousPoint(mapPtr)); // add featImgPnt in constructor
							lmkPtr->setId();
							lmkPtr->linkToParentMap(mapPtr);

							// 2b. create obs object
							observation_ptr_t obsPtr = obsFact.create(senPtr,lmkPtr);
							obsPtr->setId();

							// 2c. fill data for this obs
							obsPtr->events.visible = true;
							obsPtr->events.measured = true;
							obsPtr->measurement.x(featPtr->measurement.x());

							// 2d. compute and fill stochastic data for the landmark
							obsPtr->backProject();

							// 2e. Create lmk descriptor
							vec7 globalSensorPose = senPtr->globalPose();
							desc_img_pnt_ptr_t descPtr(new DescriptorImagePoint(featPtr, globalSensorPose, obsPtr));
							lmkPtr->setDescriptor(descPtr);

							// Complete SLAM graph with all other obs
							// mapPtr->completeObservationsInGraph(senPtr, lmkPtr); // FIXME

//							cout << "\n-------------------------------------------------- " << endl;
//							cout << *lmkPtr << endl;
//							cout << "Initialized lmk: " << lmkPtr->id() << endl;

						} // detect()
					} // getROI()
				} // unusedStates()

				had_data = true;

			} // for each sensor
		} // for each robot



		if (had_data) {
			t++;

			if (robPtr1->dt_or_dx > max_dt) max_dt = robPtr1->dt_or_dx;

			// Output info
			cout << "dt: " << (int) (1000 * robPtr1->dt_or_dx) << "ms (match "
					<< total_match_time/1000 << " ms, update " << total_update_time/1000 << "ms). Lmk: [";
			cout << mapPtr->landmarkList().size() << "] ";
			for (MapAbstract::LandmarkList::iterator lmkIter =
			    mapPtr->landmarkList().begin(); lmkIter
			    != mapPtr->landmarkList().end(); lmkIter++) {
				cout << (*lmkIter)->id() << " ";
			}

			// Landmark reparametrization and deletion management
			cout << " ; ";
			for (MapAbstract::LandmarkList::iterator lmkIter =
			    mapPtr->landmarkList().begin(); lmkIter != mapPtr->landmarkList().end(); lmkIter++) {
				landmark_ptr_t lmkPtr = *lmkIter;
				if (lmkPtr->needToDie()) {
					cout << "-" << lmkPtr->id() << " ";
					lmkIter++; // move iterator before killing list member
					lmkPtr->suicide();
					lmkIter--;
				}
				else{
					if (lmkPtr->needToReparametrize()){
						cout << "R" << lmkPtr->id() << " ";
//						lmkIter++; // move iterator before killing list member
//						lmkPtr->reparametrize();
//						lmkIter--; // restore iterator after killing list member
					}
				}
			}
			cout << endl;
			//			vec7 F = robPtr1->pose.x();
			//			vec3 p = subrange(F, 0, 3);
			//			vec4 q = subrange(F, 3, 7);
			//			vec3 e = quaternion::q2e(q);
			//			cout << "p: " << 100 * p << " cm. e: " << (180.0 / 3.14) * e << " deg." << endl;

		} // if had_data


		worldPtr->display_mutex.unlock();
		mutex_chrono.reset();

	} // temporal loop

	cout << "time avg(max): " << total_chrono.elapsed()/N_FRAMES << "(" << (int)(1000*max_dt) <<") ms" << endl;
	std::cout << "\nFINISHED !" << std::endl;

	sleep(60);

} // demo_slam01_main


void demo_slam01_display(world_ptr_t *world)
{
	//(*world)->display_mutex.lock();
	qdisplay::qtMutexLock<kernel::FifoMutex>((*world)->display_mutex);
	display::ViewerQt *viewerQt = static_cast<display::ViewerQt*>((*world)->getDisplayViewer(display::ViewerQt::id()));
	if (viewerQt == NULL)
	{
		viewerQt = new display::ViewerQt();
		(*world)->addDisplayViewer(viewerQt, display::ViewerQt::id());
	}
	viewerQt->bufferize(*world);
	(*world)->display_mutex.unlock();
	
	viewerQt->render();
}


void demo_slam01(bool display) {
	world_ptr_t worldPtr(new WorldAbstract());
	
	// to start with qt display
	const int slam_priority = -20; // needs to be started as root to be < 0
	const int display_priority = 10;
	const int display_period = 100; // ms
	if (display)
	{
		qdisplay::QtAppStart((qdisplay::FUNC)&demo_slam01_display,display_priority,(qdisplay::FUNC)&demo_slam01_main,slam_priority,display_period,&worldPtr);
	} else
	{
		kernel::setCurrentThreadPriority(slam_priority);
		demo_slam01_main(&worldPtr);
	}

	JFR_DEBUG("Terminated");
}

/**
 * Function call usage:
 *
 * 	demo_slam DISP DUMP PATH
 *
 * If you want display, pass a first argument DISP="1" to the executable, otherwise "0".
 * If you want to dump images, pass a second argument to the executable DUMP="1" and a path where
 * you want the processed images be saved. If you want to replay the last execution, change 1 to 2
 *
 * Example 1: demo_slam 1 1 /home/you/rtslam dumps images to /home/you/rtslam
 * example 2: demo_slam 1 2 /home/you/rtslam replays the saved sequence
 * example 3: demo_slam 1 0 /anything does not dump
 * example 4: demo_slam 0 ahy /anything does not display nor dump.
 */
int main(int argc, const char* argv[])
{
	bool display = 1;
	if (argc == 4)
	{
		display = atoi(argv[1]);
		mode = atoi(argv[2]);
		dump_path = argv[3];
	} else if (argc != 0)
		std::cout << "Usage: demo_slam <display-enabled=1> <image-mode=0> <dump-path=.>" << std::endl;
	
	demo_slam01(display);
}
