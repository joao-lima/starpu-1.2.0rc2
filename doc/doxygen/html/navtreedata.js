var NAVTREE =
[
  [ "StarPU Handbook", "index.html", [
    [ "Introduction", "index.html", [
      [ "Motivation", "index.html#Motivation", null ],
      [ "StarPU in a Nutshell", "index.html#StarPUInANutshell", [
        [ "Codelet and Tasks", "index.html#CodeletAndTasks", null ],
        [ "StarPU Data Management Library", "index.html#StarPUDataManagementLibrary", null ]
      ] ],
      [ "Application Taskification", "index.html#ApplicationTaskification", null ],
      [ "Glossary", "index.html#Glossary", null ],
      [ "Research Papers", "index.html#ResearchPapers", null ],
      [ "StarPU Applications", "index.html#StarPUApplications", null ],
      [ "Further Reading", "index.html#FurtherReading", null ]
    ] ],
    [ "Building and Installing StarPU", "BuildingAndInstallingStarPU.html", [
      [ "Installing a Binary Package", "BuildingAndInstallingStarPU.html#InstallingABinaryPackage", null ],
      [ "Installing from Source", "BuildingAndInstallingStarPU.html#InstallingFromSource", [
        [ "Optional Dependencies", "BuildingAndInstallingStarPU.html#OptionalDependencies", null ],
        [ "Getting Sources", "BuildingAndInstallingStarPU.html#GettingSources", null ],
        [ "Configuring StarPU", "BuildingAndInstallingStarPU.html#ConfiguringStarPU", null ],
        [ "Building StarPU", "BuildingAndInstallingStarPU.html#BuildingStarPU", null ],
        [ "Installing StarPU", "BuildingAndInstallingStarPU.html#InstallingStarPU", null ]
      ] ],
      [ "Setting up Your Own Code", "BuildingAndInstallingStarPU.html#SettingUpYourOwnCode", [
        [ "Setting Flags for Compiling, Linking and Running Applications", "BuildingAndInstallingStarPU.html#SettingFlagsForCompilingLinkingAndRunningApplications", null ],
        [ "Running a Basic StarPU Application", "BuildingAndInstallingStarPU.html#RunningABasicStarPUApplication", null ],
        [ "Running a Basic StarPU Application on Microsoft Visual C", "BuildingAndInstallingStarPU.html#RunningABasicStarPUApplicationOnMicrosoft", null ],
        [ "Kernel Threads Started by StarPU", "BuildingAndInstallingStarPU.html#KernelThreadsStartedByStarPU", null ],
        [ "Enabling OpenCL", "BuildingAndInstallingStarPU.html#EnablingOpenCL", null ]
      ] ],
      [ "Benchmarking StarPU", "BuildingAndInstallingStarPU.html#BenchmarkingStarPU", [
        [ "Task Size Overhead", "BuildingAndInstallingStarPU.html#TaskSizeOverhead", null ],
        [ "Data Transfer Latency", "BuildingAndInstallingStarPU.html#DataTransferLatency", null ],
        [ "Matrix-Matrix Multiplication", "BuildingAndInstallingStarPU.html#MatrixMatrixMultiplication", null ],
        [ "Cholesky Factorization", "BuildingAndInstallingStarPU.html#CholeskyFactorization", null ],
        [ "LU Factorization", "BuildingAndInstallingStarPU.html#LUFactorization", null ],
        [ "Simulated benchmarks", "BuildingAndInstallingStarPU.html#SimulatedBenchmarks", null ]
      ] ]
    ] ],
    [ "Basic Examples", "BasicExamples.html", [
      [ "Hello World Using The C Extension", "BasicExamples.html#HelloWorldUsingTheCExtension", null ],
      [ "Hello World Using StarPU's API", "BasicExamples.html#HelloWorldUsingStarPUAPI", [
        [ "Required Headers", "BasicExamples.html#RequiredHeaders", null ],
        [ "Defining A Codelet", "BasicExamples.html#DefiningACodelet", null ],
        [ "Submitting A Task", "BasicExamples.html#SubmittingATask", null ],
        [ "Execution Of Hello World", "BasicExamples.html#ExecutionOfHelloWorld", null ],
        [ "Passing Arguments To The Codelet", "BasicExamples.html#PassingArgumentsToTheCodelet", null ],
        [ "Defining A Callback", "BasicExamples.html#DefiningACallback", null ],
        [ "Where To Execute A Codelet", "BasicExamples.html#WhereToExecuteACodelet", null ]
      ] ],
      [ "Vector Scaling Using the C Extension", "BasicExamples.html#VectorScalingUsingTheCExtension", [
        [ "Adding an OpenCL Task Implementation", "BasicExamples.html#AddingAnOpenCLTaskImplementation", null ],
        [ "Adding a CUDA Task Implementation", "BasicExamples.html#AddingACUDATaskImplementation", null ]
      ] ],
      [ "Vector Scaling Using StarPU's API", "BasicExamples.html#VectorScalingUsingStarPUAPI", [
        [ "Source Code of Vector Scaling", "BasicExamples.html#SourceCodeOfVectorScaling", null ],
        [ "Execution of Vector Scaling", "BasicExamples.html#ExecutionOfVectorScaling", null ]
      ] ],
      [ "Vector Scaling on an Hybrid CPU/GPU Machine", "BasicExamples.html#VectorScalingOnAnHybridCPUGPUMachine", [
        [ "Definition of the CUDA Kernel", "BasicExamples.html#DefinitionOfTheCUDAKernel", null ],
        [ "Definition of the OpenCL Kernel", "BasicExamples.html#DefinitionOfTheOpenCLKernel", null ],
        [ "Definition of the Main Code", "BasicExamples.html#DefinitionOfTheMainCode", null ],
        [ "Execution of Hybrid Vector Scaling", "BasicExamples.html#ExecutionOfHybridVectorScaling", null ]
      ] ]
    ] ],
    [ "Advanced Examples", "AdvancedExamples.html", null ],
    [ "Check List When Performance Are Not There", "CheckListWhenPerformanceAreNotThere.html", [
      [ "Data Related Features That May Improve Performance", "CheckListWhenPerformanceAreNotThere.html#DataRelatedFeaturesToImprovePerformance", null ],
      [ "Task Related Features That May Improve Performance", "CheckListWhenPerformanceAreNotThere.html#TaskRelatedFeaturesToImprovePerformance", null ],
      [ "Scheduling Related Features That May Improve Performance", "CheckListWhenPerformanceAreNotThere.html#SchedulingRelatedFeaturesToImprovePerformance", null ],
      [ "CUDA-specific Optimizations", "CheckListWhenPerformanceAreNotThere.html#CUDA-specificOptimizations", null ],
      [ "OpenCL-specific Optimizations", "CheckListWhenPerformanceAreNotThere.html#OpenCL-specificOptimizations", null ],
      [ "Detection Stuck Conditions", "CheckListWhenPerformanceAreNotThere.html#DetectionStuckConditions", null ],
      [ "How to limit memory per node", "CheckListWhenPerformanceAreNotThere.html#HowToLimitMemoryPerNode", null ],
      [ "How To Reduce The Memory Footprint Of Internal Data Structures", "CheckListWhenPerformanceAreNotThere.html#HowToReduceTheMemoryFootprintOfInternalDataStructures", null ],
      [ "How to reuse memory", "CheckListWhenPerformanceAreNotThere.html#HowtoReuseMemory", null ],
      [ "Performance Model Calibration", "CheckListWhenPerformanceAreNotThere.html#PerformanceModelCalibration", null ],
      [ "Profiling", "CheckListWhenPerformanceAreNotThere.html#Profiling", null ]
    ] ],
    [ "Tasks In StarPU", "TasksInStarPU.html", [
      [ "Task Granularity", "TasksInStarPU.html#TaskGranularity", null ],
      [ "Task Submission", "TasksInStarPU.html#TaskSubmission", null ],
      [ "Task Priorities", "TasksInStarPU.html#TaskPriorities", null ],
      [ "Setting The Data Handles For A Task", "TasksInStarPU.html#SettingTheDataHandlesForATask", null ],
      [ "Using Multiple Implementations Of A Codelet", "TasksInStarPU.html#UsingMultipleImplementationsOfACodelet", null ],
      [ "Enabling Implementation According To Capabilities", "TasksInStarPU.html#EnablingImplementationAccordingToCapabilities", null ],
      [ "Insert Task Utility", "TasksInStarPU.html#InsertTaskUtility", null ],
      [ "Parallel Tasks", "TasksInStarPU.html#ParallelTasks", [
        [ "Fork-mode Parallel Tasks", "TasksInStarPU.html#Fork-modeParallelTasks", null ],
        [ "SPMD-mode Parallel Tasks", "TasksInStarPU.html#SPMD-modeParallelTasks", null ],
        [ "Parallel Tasks Performance", "TasksInStarPU.html#ParallelTasksPerformance", null ],
        [ "Combined Workers", "TasksInStarPU.html#CombinedWorkers", null ],
        [ "Concurrent Parallel Tasks", "TasksInStarPU.html#ConcurrentParallelTasks", null ]
      ] ]
    ] ],
    [ "Data Management", "DataManagement.html", [
      [ "Data Management", "DataManagement.html#DataManagement", null ],
      [ "Data Prefetch", "DataManagement.html#DataPrefetch", null ],
      [ "Partitioning Data", "DataManagement.html#PartitioningData", null ],
      [ "Data Reduction", "DataManagement.html#DataReduction", null ],
      [ "Commute Data Access", "DataManagement.html#DataCommute", null ],
      [ "Concurrent Data accesses", "DataManagement.html#ConcurrentDataAccess", null ],
      [ "Temporary Buffers", "DataManagement.html#TemporaryBuffers", [
        [ "Temporary Data", "DataManagement.html#TemporaryData", null ],
        [ "Scratch Data", "DataManagement.html#ScratchData", null ]
      ] ],
      [ "The Multiformat Interface", "DataManagement.html#TheMultiformatInterface", null ],
      [ "Defining A New Data Interface", "DataManagement.html#DefiningANewDataInterface", null ],
      [ "Specifying a target node for task data", "DataManagement.html#SpecifyingATargetNode", null ]
    ] ],
    [ "Scheduling", "Scheduling.html", [
      [ "Task Scheduling Policy", "Scheduling.html#TaskSchedulingPolicy", null ],
      [ "Task Distribution Vs Data Transfer", "Scheduling.html#TaskDistributionVsDataTransfer", null ],
      [ "Power-based Scheduling", "Scheduling.html#Power-basedScheduling", null ],
      [ "Static Scheduling", "Scheduling.html#StaticScheduling", null ],
      [ "Defining A New Scheduling Policy", "Scheduling.html#DefiningANewSchedulingPolicy", null ]
    ] ],
    [ "Scheduling Contexts", "SchedulingContexts.html", [
      [ "General Ideas", "SchedulingContexts.html#GeneralIdeas", null ],
      [ "Creating A Context", "SchedulingContexts.html#CreatingAContext", null ],
      [ "Modifying A Context", "SchedulingContexts.html#ModifyingAContext", null ],
      [ "Submitting Tasks To A Context", "SchedulingContexts.html#SubmittingTasksToAContext", null ],
      [ "Deleting A Context", "SchedulingContexts.html#DeletingAContext", null ],
      [ "Emptying A Context", "SchedulingContexts.html#EmptyingAContext", null ],
      [ "Contexts Sharing Workers", "SchedulingContexts.html#ContextsSharingWorkers", null ]
    ] ],
    [ "Scheduling Context Hypervisor", "SchedulingContextHypervisor.html", [
      [ "What Is The Hypervisor", "SchedulingContextHypervisor.html#WhatIsTheHypervisor", null ],
      [ "Start the Hypervisor", "SchedulingContextHypervisor.html#StartTheHypervisor", null ],
      [ "Interrogate The Runtime", "SchedulingContextHypervisor.html#InterrogateTheRuntime", null ],
      [ "Trigger the Hypervisor", "SchedulingContextHypervisor.html#TriggerTheHypervisor", null ],
      [ "Resizing Strategies", "SchedulingContextHypervisor.html#ResizingStrategies", null ],
      [ "Defining A New Hypervisor Policy", "SchedulingContextHypervisor.html#DefiningANewHypervisorPolicy", null ]
    ] ],
    [ "Debugging Tools", "DebuggingTools.html", [
      [ "Using The Temanejo Task Debugger", "DebuggingTools.html#UsingTheTemanejoTaskDebugger", null ]
    ] ],
    [ "Online Performance Tools", "OnlinePerformanceTools.html", [
      [ "On-line Performance Feedback", "OnlinePerformanceTools.html#On-linePerformanceFeedback", [
        [ "Enabling On-line Performance Monitoring", "OnlinePerformanceTools.html#EnablingOn-linePerformanceMonitoring", null ],
        [ "Per-task Feedback", "OnlinePerformanceTools.html#Per-taskFeedback", null ],
        [ "Per-codelet Feedback", "OnlinePerformanceTools.html#Per-codeletFeedback", null ],
        [ "Per-worker Feedback", "OnlinePerformanceTools.html#Per-workerFeedback", null ],
        [ "Bus-related Feedback", "OnlinePerformanceTools.html#Bus-relatedFeedback", null ],
        [ "StarPU-Top Interface", "OnlinePerformanceTools.html#StarPU-TopInterface", null ]
      ] ],
      [ "Task And Worker Profiling", "OnlinePerformanceTools.html#TaskAndWorkerProfiling", null ],
      [ "Performance Model Example", "OnlinePerformanceTools.html#PerformanceModelExample", null ],
      [ "Data trace and tasks length", "OnlinePerformanceTools.html#DataTrace", null ]
    ] ],
    [ "Offline Performance Tools", "OfflinePerformanceTools.html", [
      [ "Off-line Performance Feedback", "OfflinePerformanceTools.html#Off-linePerformanceFeedback", [
        [ "Generating Traces With FxT", "OfflinePerformanceTools.html#GeneratingTracesWithFxT", null ],
        [ "Creating a Gantt Diagram", "OfflinePerformanceTools.html#CreatingAGanttDiagram", null ],
        [ "Creating a DAG With Graphviz", "OfflinePerformanceTools.html#CreatingADAGWithGraphviz", null ],
        [ "Monitoring Activity", "OfflinePerformanceTools.html#MonitoringActivity", null ]
      ] ],
      [ "Performance Of Codelets", "OfflinePerformanceTools.html#PerformanceOfCodelets", null ],
      [ "Trace statistics", "OfflinePerformanceTools.html#TraceStatistics", null ],
      [ "Theoretical Lower Bound On Execution Time", "OfflinePerformanceTools.html#TheoreticalLowerBoundOnExecutionTime", null ],
      [ "Theoretical Lower Bound On Execution Time Example", "OfflinePerformanceTools.html#TheoreticalLowerBoundOnExecutionTimeExample", null ],
      [ "Memory Feedback", "OfflinePerformanceTools.html#MemoryFeedback", null ],
      [ "Data Statistics", "OfflinePerformanceTools.html#DataStatistics", null ]
    ] ],
    [ "Frequently Asked Questions", "FrequentlyAskedQuestions.html", [
      [ "How To Initialize A Computation Library Once For Each Worker?", "FrequentlyAskedQuestions.html#HowToInitializeAComputationLibraryOnceForEachWorker", null ],
      [ "Using The Driver API", "FrequentlyAskedQuestions.html#UsingTheDriverAPI", null ],
      [ "On-GPU Rendering", "FrequentlyAskedQuestions.html#On-GPURendering", null ],
      [ "Using StarPU With MKL 11 (Intel Composer XE 2013)", "FrequentlyAskedQuestions.html#UsingStarPUWithMKL", null ],
      [ "Thread Binding on NetBSD", "FrequentlyAskedQuestions.html#ThreadBindingOnNetBSD", null ],
      [ "Interleaving StarPU and non-StarPU code", "FrequentlyAskedQuestions.html#PauseResume", null ]
    ] ],
    [ "Out Of Core", "OutOfCore.html", [
      [ "Introduction", "OutOfCore.html#Introduction", null ],
      [ "Use a new disk memory", "OutOfCore.html#UseANewDiskMemory", null ],
      [ "Disk functions", "OutOfCore.html#DiskFunctions", null ],
      [ "Examples: disk_copy", "OutOfCore.html#ExampleDiskCopy", null ],
      [ "Examples: disk_compute", "OutOfCore.html#ExampleDiskCompute", null ]
    ] ],
    [ "MPI Support", "MPISupport.html", [
      [ "Simple Example", "MPISupport.html#SimpleExample", null ],
      [ "Point To Point Communication", "MPISupport.html#PointToPointCommunication", null ],
      [ "Exchanging User Defined Data Interface", "MPISupport.html#ExchangingUserDefinedDataInterface", null ],
      [ "MPI Insert Task Utility", "MPISupport.html#MPIInsertTaskUtility", null ],
      [ "MPI cache support", "MPISupport.html#MPICache", null ],
      [ "MPI Data migration", "MPISupport.html#MPIMigration", null ],
      [ "MPI Collective Operations", "MPISupport.html#MPICollective", null ]
    ] ],
    [ "FFT Support", "FFTSupport.html", [
      [ "Compilation", "FFTSupport.html#Compilation", null ]
    ] ],
    [ "MIC Xeon Phi / SCC Support", "MICSCCSupport.html", [
      [ "Porting Applications To MIC Xeon Phi / SCC", "MICSCCSupport.html#PortingApplicationsToMICSCC", null ],
      [ "Launching Programs", "MICSCCSupport.html#LaunchingPrograms", null ]
    ] ],
    [ "C Extensions", "cExtensions.html", [
      [ "Defining Tasks", "cExtensions.html#DefiningTasks", null ],
      [ "Initialization, Termination, and Synchronization", "cExtensions.html#InitializationTerminationAndSynchronization", null ],
      [ "Registered Data Buffers", "cExtensions.html#RegisteredDataBuffers", null ],
      [ "Using C Extensions Conditionally", "cExtensions.html#UsingCExtensionsConditionally", null ]
    ] ],
    [ "SOCL OpenCL Extensions", "SOCLOpenclExtensions.html", null ],
    [ "SimGrid Support", "SimGridSupport.html", [
      [ "Preparing your application for simulation.", "SimGridSupport.html#Preparing", null ],
      [ "Calibration", "SimGridSupport.html#Calibration", null ],
      [ "Simulation", "SimGridSupport.html#Simulation", null ],
      [ "Simulation On Another Machine", "SimGridSupport.html#SimulationOnAnotherMachine", null ],
      [ "Simulation examples", "SimGridSupport.html#SimulationExamples", null ],
      [ "simulation", "SimGridSupport.html#Tweaking", null ],
      [ "applications", "SimGridSupport.html#MPI", null ],
      [ "applications", "SimGridSupport.html#Debugging", null ]
    ] ],
    [ "The StarPU OpenMP Runtime Support (SORS)", "OpenMPRuntimeSupport.html", [
      [ "Implementation Details and Specificities", "OpenMPRuntimeSupport.html#Implementation", [
        [ "Main Thread", "OpenMPRuntimeSupport.html#MainThread", null ],
        [ "Extended Task Semantics", "OpenMPRuntimeSupport.html#TaskSemantics", null ]
      ] ],
      [ "Configuration", "OpenMPRuntimeSupport.html#Configuration", null ],
      [ "Initialization and Shutdown", "OpenMPRuntimeSupport.html#InitExit", null ],
      [ "Parallel Regions and Worksharing", "OpenMPRuntimeSupport.html#Parallel", [
        [ "Parallel Regions", "OpenMPRuntimeSupport.html#OMPParallel", null ],
        [ "Parallel For", "OpenMPRuntimeSupport.html#OMPFor", null ],
        [ "Sections", "OpenMPRuntimeSupport.html#OMPSections", null ],
        [ "Single", "OpenMPRuntimeSupport.html#OMPSingle", null ]
      ] ],
      [ "Tasks", "OpenMPRuntimeSupport.html#Task", [
        [ "Explicit Tasks", "OpenMPRuntimeSupport.html#OMPTask", null ],
        [ "Data Dependencies", "OpenMPRuntimeSupport.html#DataDependencies", null ],
        [ "TaskWait and TaskGroup", "OpenMPRuntimeSupport.html#TaskSyncs", null ]
      ] ],
      [ "Synchronization Support", "OpenMPRuntimeSupport.html#Synchronization", [
        [ "Simple Locks", "OpenMPRuntimeSupport.html#SimpleLock", null ],
        [ "Nestable Locks", "OpenMPRuntimeSupport.html#NestableLock", null ],
        [ "Critical Sections", "OpenMPRuntimeSupport.html#Critical", null ],
        [ "Barriers", "OpenMPRuntimeSupport.html#Barrier", null ]
      ] ]
    ] ],
    [ "Execution Configuration Through Environment Variables", "ExecutionConfigurationThroughEnvironmentVariables.html", [
      [ "Configuring Workers", "ExecutionConfigurationThroughEnvironmentVariables.html#ConfiguringWorkers", null ],
      [ "Configuring The Scheduling Engine", "ExecutionConfigurationThroughEnvironmentVariables.html#ConfiguringTheSchedulingEngine", null ],
      [ "Extensions", "ExecutionConfigurationThroughEnvironmentVariables.html#Extensions", null ],
      [ "Miscellaneous And Debug", "ExecutionConfigurationThroughEnvironmentVariables.html#MiscellaneousAndDebug", null ],
      [ "Configuring The Hypervisor", "ExecutionConfigurationThroughEnvironmentVariables.html#ConfiguringTheHypervisor", null ]
    ] ],
    [ "Compilation Configuration", "CompilationConfiguration.html", [
      [ "Common Configuration", "CompilationConfiguration.html#CommonConfiguration", null ],
      [ "Extension Configuration", "CompilationConfiguration.html#ExtensionConfiguration", null ],
      [ "Advanced Configuration", "CompilationConfiguration.html#AdvancedConfiguration", null ]
    ] ],
    [ "Files", "Files.html", null ],
    [ "Full source code for the ’Scaling a Vector’ example", "FullSourceCodeVectorScal.html", [
      [ "Main Application", "FullSourceCodeVectorScal.html#MainApplication", null ],
      [ "CPU Kernel", "FullSourceCodeVectorScal.html#CPUKernel", null ],
      [ "CUDA Kernel", "FullSourceCodeVectorScal.html#CUDAKernel", null ],
      [ "OpenCL Kernel", "FullSourceCodeVectorScal.html#OpenCLKernel", [
        [ "Invoking the Kernel", "FullSourceCodeVectorScal.html#InvokingtheKernel", null ],
        [ "Source of the Kernel", "FullSourceCodeVectorScal.html#SourceoftheKernel", null ]
      ] ]
    ] ],
    [ "The GNU Free Documentation License", "GNUFreeDocumentationLicense.html", [
      [ "ADDENDUM: How to use this License for your documents", "GNUFreeDocumentationLicense.html#ADDENDUM", null ]
    ] ],
    [ "Modularized Schedulers", "ModularizedScheduler.html", [
      [ "Using Modularized Schedulers", "ModularizedScheduler.html#UsingModularizedSchedulers", [
        [ "Existing Modularized Schedulers", "ModularizedScheduler.html#ExistingModularizedSchedulers", null ],
        [ "An Example : The Tree-Eager-Prefetching Strategy", "ModularizedScheduler.html#ExampleTreeEagerPrefetchingStrategy", null ],
        [ "Interface", "ModularizedScheduler.html#Interface", null ]
      ] ],
      [ "Build a Modularized Scheduler", "ModularizedScheduler.html#BuildAModularizedScheduler", [
        [ "Pre-implemented Components", "ModularizedScheduler.html#PreImplementedComponents", null ],
        [ "Progression And Validation Rules", "ModularizedScheduler.html#ProgressionAndValidationRules", null ],
        [ "Implement a Modularized Scheduler", "ModularizedScheduler.html#ImplementAModularizedScheduler", null ]
      ] ],
      [ "Write a Scheduling Component", "ModularizedScheduler.html#WriteASchedulingComponent", [
        [ "Generic Scheduling Component", "ModularizedScheduler.html#GenericSchedulingComponent", null ],
        [ "Instantiation : Redefine the Interface", "ModularizedScheduler.html#InstantiationRedefineInterface", null ],
        [ "Detailed Progression and Validation Rules", "ModularizedScheduler.html#DetailedProgressionAndValidationRules", null ]
      ] ]
    ] ],
    [ "Deprecated List", "deprecated.html", null ],
    [ "Modules", "modules.html", "modules" ],
    [ "Data Structures", null, [
      [ "Data Structures", "annotated.html", "annotated" ],
      [ "Data Structure Index", "classes.html", null ],
      [ "Data Fields", "functions.html", [
        [ "All", "functions.html", "functions_dup" ],
        [ "Variables", "functions_vars.html", "functions_vars" ]
      ] ]
    ] ],
    [ "Files", null, [
      [ "File List", "files.html", "files" ],
      [ "Globals", "globals.html", [
        [ "All", "globals.html", "globals_dup" ],
        [ "Functions", "globals_func.html", "globals_func" ],
        [ "Variables", "globals_vars.html", null ],
        [ "Typedefs", "globals_type.html", null ],
        [ "Enumerations", "globals_enum.html", null ],
        [ "Enumerator", "globals_eval.html", null ],
        [ "Macros", "globals_defs.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"AdvancedExamples.html",
"functions_vars_b.html",
"group__API__Codelet__And__Tasks.html#gaecb3efa04cb10b049b10c6166dd9c30c",
"group__API__Data__Interfaces.html#ga988036ae1f9385832c2988064a4f27af",
"group__API__Data__Partition.html#ga6a3f729055f14384e7397d2815a2c9a5",
"group__API__MPI__Support.html#ga11f71926b324d325d8a6f6b72b58cdf1",
"group__API__OpenCL__Extensions.html#ga1b591248c13dc33e2e9b00ace593405e",
"group__API__OpenMP__Runtime__Support.html#ggacc6078a78820c367f07d37da11c04520a86961832c7cc5d9f7821143e1c185257",
"group__API__SCC__Extensions.html#ga651a759ee07740d52e96d15ca3c68ae8",
"group__API__Scheduling__Policy.html#a1b2449ac50ca8db35234a36a3f77b0b5",
"group__API__Theoretical__Lower__Bound__on__Execution__Time.html#ga5f1859599a28105aea4c0f33fd871218",
"group__API__Workers__Properties.html#ga80b06886ee8a4c0e99b09ab638113af3",
"starpu__data__interfaces_8h.html#gaa2f2140147f15e7b9eec1443690e357ca61676e55a5c706ecb014c772a39e292d"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';