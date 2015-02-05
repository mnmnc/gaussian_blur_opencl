#include <iostream>
#include <string>
#include "lodepng.h"
#include <math.h>
#include <ctime>
#include <CL\cl.h>
#include <fstream>
#include <vector>


#define DIM 5

using namespace std;

std::vector<unsigned char> decodeOneStep(const char* filename)
{
	std::vector<unsigned char> image; //the raw pixels
	unsigned width, height;
	unsigned error = lodepng::decode(image, width, height, filename);
	if (error) std::cout << "decoder error " << error << ": " << lodepng_error_text(error) << std::endl;
	return image;
}

vector<double> create_convolution_matrix(double sigma){
	/*
	Creates convolution matrix for Gaussian blur given sigma and dimention of the filter.
	*/


	int W = DIM;
	double kernel[DIM][DIM];
	vector<double> result;
	double mean = W / 2;
	double sum = 0.0; // For accumulating the kernel values
	for (int x = 0; x < W; ++x)
		for (int y = 0; y < W; ++y) {
		kernel[x][y] =
			exp(-0.5 * (pow((x - mean) / sigma, 2.0) +
			pow((y - mean) / sigma, 2.0))) /
			(2 * 3.14159 * sigma * sigma);

		// Accumulate the kernel values
		sum += kernel[x][y];
		}

	// Normalize the kernel
	for (int x = 0; x < W; ++x)
		for (int y = 0; y < W; ++y)
			kernel[x][y] /= sum;

	for (int x = 0; x < W; ++x) {
		for (int y = 0; y < W; ++y) {
			result.push_back(kernel[x][y]);
		}
	}
	return result;
}

void encodeOneStep(const char* filename, std::vector<unsigned char>& image, unsigned width, unsigned height)
{
	//Encode the image
	unsigned error = lodepng::encode(filename, image, width, height);

	//if there's an error, display it
	if (error) std::cout << "encoder error " << error << ": " << lodepng_error_text(error) << std::endl;
}

std::vector<unsigned char> paint_pixel_white(std::vector<unsigned char> image, int x){
	// Function helpful for debugig
	std::vector<unsigned char> nimage = image;
	int ref = x * 4;

	for (int i = 0; i < 3; ++i){
		nimage[ref + i] = 255;
	}
	return nimage;
}


int main(){

	// VARIABLES
	char * filename = "3.png";
	char * filename_out = "8.png";
	int width = 678;
	int height = 353;

	// GAUSSIAN VARIABLES
	double gauss_sigma = 1;
	int gauss_filter_dimention = DIM;

	// ORIGINAL IMAGE
	std::vector<unsigned char> image = decodeOneStep(filename);

	// IMAGE AFTER PROCESSING
	std::vector<unsigned char> nimage = image;

	// CREATE CONVOLUTION MATRIX
	vector<double> matrix = create_convolution_matrix(gauss_sigma);

	// CONVOLUTION MATRIX TO NORMAL ARRAY
	float flat_matrix[DIM*DIM];
	for (int i = 0; i < matrix.size(); ++i){
		flat_matrix[i] = matrix.at(i);
	}

	// IMAGE TO NORMAL ARRAY
	unsigned char * flat_image = new unsigned char[width * height * 4];
	for (int i = 0; i < image.size(); ++i){
		flat_image[i] = image.at(i);
	}

	// BLURRED IMAGE ARRAY
	unsigned char * flat_image_blurred = new unsigned char[width * height * 4];


	//
	// O P E N   C L 
	//
	//Get platform and device information
	cl_int ret;					//the openCL error code/s
	cl_platform_id platformID;	//will hold the ID of the openCL available platform
	cl_uint platformsN;			//will hold the number of openCL available platforms on the machine
	cl_device_id deviceID;		//will hold the ID of the openCL device
	cl_uint devicesN;			//will hold the number of OpenCL devices in the system
	const cl_device_type kDeviceType = CL_DEVICE_TYPE_GPU; // get GPU device

	//
	// Loading Kernell
	//
	ifstream cl_file("gauss.cl");
	if (cl_file.fail())
	{
		cout << "Error opening kernel file!" << endl;
		std::system("PAUSE");
		return 1;
	}
	string kernel_string(istreambuf_iterator<char>(cl_file), (istreambuf_iterator<char>()));
	const char *src = kernel_string.c_str();



	if (clGetPlatformIDs(1, &platformID, &platformsN) != CL_SUCCESS)
	{
		printf("Could not get the OpenCL Platform IDs\n");
		system("PAUSE");
		return false;
	}
	if (clGetDeviceIDs(platformID, kDeviceType, 1, &deviceID, &devicesN) != CL_SUCCESS)
	{
		printf("Could not get the system's OpenCL device\n");
		system("PAUSE");
		return false;
	}

	// Create an OpenCL context
	cl_context context = clCreateContext(NULL, 1, &deviceID, NULL, NULL, &ret);
	if (ret != CL_SUCCESS)
	{
		printf("Could not create a valid OpenCL context\n");
		system("PAUSE");
		return false;
	}
	// Create a command queue
	cl_command_queue cmdQueue = clCreateCommandQueue(context, deviceID, 0, &ret);
	if (ret != CL_SUCCESS)
	{
		printf("Could not create an OpenCL Command Queue\n");
		system("PAUSE");
		return false;
	}


	/// Create memory buffers on the device for the two images
	cl_mem gpuImg = clCreateBuffer(context, CL_MEM_READ_ONLY, width*height * 4, NULL, &ret);
	if (ret != CL_SUCCESS)
	{
		printf("Unable to create the GPU image buffer object\n");
		system("PAUSE");
		return false;
	}
	cl_mem gpuGaussian = clCreateBuffer(context, CL_MEM_READ_ONLY, DIM*DIM * 4, NULL, &ret);
	if (ret != CL_SUCCESS)
	{
		printf("Unable to create the GPU image buffer object\n");
		system("PAUSE");
		return false;
	}
	cl_mem gpuNewImg = clCreateBuffer(context, CL_MEM_WRITE_ONLY, width*height * 4, NULL, &ret);
	if (ret != CL_SUCCESS)
	{
		printf("Unable to create the GPU image buffer object\n");
		system("PAUSE");
		return false;
	}

	//Copy the image data and the gaussian kernel to the memory buffer
	if (clEnqueueWriteBuffer(cmdQueue, gpuImg, CL_TRUE, 0, width*height * 4, flat_image, 0, NULL, NULL) != CL_SUCCESS)
	{
		printf("Error during sending the image data to the OpenCL buffer\n");
		system("PAUSE");
		return false;
	}
	if (clEnqueueWriteBuffer(cmdQueue, gpuGaussian, CL_TRUE, 0, DIM*DIM * 4, flat_matrix, 0, NULL, NULL) != CL_SUCCESS)
	{
		printf("Error during sending the gaussian kernel to the OpenCL buffer\n");
		system("PAUSE");
		return false;
	}

	//Create a program object and associate it with the kernel's source code.
	cl_program program = clCreateProgramWithSource(context, 1, (const char **)&src, NULL, &ret);
	//free(kernelSource);
	if (ret != CL_SUCCESS)
	{
		printf("Error in creating an OpenCL program object\n");
		system("PAUSE");
		return false;
	}
	//Build the created OpenCL program
	if ((ret = clBuildProgram(program, 1, &deviceID, NULL, NULL, NULL)) != CL_SUCCESS)
	{
		printf("Failed to build the OpenCL program\n");
		char log[1024] = {};
		clGetProgramBuildInfo(program, deviceID, CL_PROGRAM_BUILD_LOG, 1024, log, NULL);
		cout << "Build log:\n" << log << endl;
		system("PAUSE");
		return false;
	}

	// Create the OpenCL kernel. This is basically one function of the program declared with the __kernel qualifier
	cl_kernel kernel = clCreateKernel(program, "gaussian_blur", &ret);
	if (ret != CL_SUCCESS)
	{
		printf("Failed to create the OpenCL Kernel from the built program\n");
		system("PAUSE");
		return false;
	}
	///Set the arguments of the kernel
	if (clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *)&gpuImg) != CL_SUCCESS)
	{
		printf("Could not set the kernel's \"gpuImg\" argument\n");
		system("PAUSE");
		return false;
	}
	if (clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *)&gpuGaussian) != CL_SUCCESS)
	{
		printf("Could not set the kernel's \"gpuGaussian\" argument\n");
		system("PAUSE");
		return false;
	}
	if (clSetKernelArg(kernel, 2, sizeof(int), (void *)&width) != CL_SUCCESS)
	{
		printf("Could not set the kernel's \"imageWidth\" argument\n");
		system("PAUSE");
		return false;
	}
	if (clSetKernelArg(kernel, 3, sizeof(int), (void *)&height) != CL_SUCCESS)
	{
		printf("Could not set the kernel's \"imgHeight\" argument\n");
		system("PAUSE");
		return false;
	}
	if (clSetKernelArg(kernel, 4, sizeof(int), (void*)&gauss_filter_dimention) != CL_SUCCESS)
	{
		printf("Could not set the kernel's \"gaussian size\" argument\n");
		system("PAUSE");
		return false;
	}
	if (clSetKernelArg(kernel, 5, sizeof(cl_mem), (void *)&gpuNewImg) != CL_SUCCESS)
	{
		printf("Could not set the kernel's \"gpuNewImg\" argument\n");
		system("PAUSE");
		return false;
	}

	///enqueue the kernel into the OpenCL device for execution
	size_t globalWorkItemSize = width*height * 4;//the total size of 1 dimension of the work items. Basically the whole image buffer size
	size_t workGroupSize = 64; //The size of one work group
	ret = clEnqueueNDRangeKernel(cmdQueue, kernel, 1, NULL, &globalWorkItemSize, &workGroupSize, 0, NULL, NULL);

	///Read the memory buffer of the new image on the device to the new Data local variable
	ret = clEnqueueReadBuffer(cmdQueue, gpuNewImg, CL_TRUE, 0, width*height * 4, flat_image_blurred, 0, NULL, NULL);



	///Clean up everything
	//free(flat_matrix);
	clFlush(cmdQueue);
	clFinish(cmdQueue);
	clReleaseKernel(kernel);
	clReleaseProgram(program);
	clReleaseMemObject(gpuImg);
	clReleaseMemObject(gpuGaussian);
	clReleaseMemObject(gpuNewImg);
	clReleaseCommandQueue(cmdQueue);
	clReleaseContext(context);


	for (int i = 0; i < width*height * 4; ++i){
		nimage[i] = flat_image_blurred[i];
	}

	// SAVE IMAGE
	encodeOneStep(filename_out, nimage, width, height);

	system("PAUSE");
	return 0;
}
