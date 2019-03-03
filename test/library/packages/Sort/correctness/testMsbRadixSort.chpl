use BitOps;
 use Sort;
 use Random;
 //use OtherMSBRadix;
 use Time;

 config const printStats = true;
 config const size = 10000;

 record uintCriterion8 {
   inline
   proc keyPart(x, start:int):(int(8),uint(8)) {
     var section:int(8) = if start > 8 then -1:int(8) else 0;
     var key:uint = (x:uint) >> (64 - 8*start);
     //writef("in keyPart8(%016xu, %u)\n", x, start);
     return (section, (key & 0xff):uint(8));
   }
 }
 record uintCriterion16 {
   inline
   proc keyPart(x, start:int):(int(8),uint(16)) {
     var section:int(8) = if start > 4 then -1:int(8) else 0;
     var key:uint = (x:uint) >> (64 - 16*start);
     //writef("in keyPart16(%016xu, %u)\n", x, start);
     return (section, (key & 0xffff):uint(16));
   }
 }
 record uintCriterion32 {
   inline
   proc keyPart(x, start:int):(int, uint(32)) {
     var section = if start > 2 then -1 else 0;
     var key:uint = (x:uint) >> (64 - 32*start);
     //writef("in keyPart32(%016xu, %u)\n", x, start);
     return (section, (key & 0xffffffff):uint(32));
   }
 }
 record uintCriterion64 {
   inline
   proc keyPart(x, start:int):(int(8), uint(64)) {
     var section:int(8) = if start > 1 then -1:int(8) else 0;
     var key:uint = x:uint;
     //writef("in keyPart64(%016xu, %u)\n", x, start);
     return (section, key);
   }
 }
 record intCriterion {
   inline
   proc keyPart(x, start:int):(int, int) {
     var section = if start > 1 then -1 else 0;
     //writef("in intCriterion64(%016xu, %u)\n", x, start);
     return (section, x);
   }
 }
 record intTupleCriterion {
   inline
   proc keyPart(x:2*int, start:int):(int, int) {
     if start > 2 then
       return (-1, 0);

     //writef("in intTupleCriterion(%016xu %016xu %u)\n", x(1), x(2), start);
     return (0, x(start));
   }
 }
 record stringCriterion {
   inline
   proc keyPart(x:string, start:int):(int(8), uint(8)) {
     var ptr = x.c_str():c_ptr(uint(8));
     var len = x.length;
     var section = if start <= len then 0:int(8)     else -1:int(8);
     var part =    if start <= len then ptr[start-1] else  0:uint(8);
     return (section, part);
   }
 }

 proc testSorted(A, comparator) {
   assert(isSorted(A, comparator));
   assert(isSorted(A));
 }
 proc testReverseSorted(A, comparator) {
   assert(isSorted(A, new ReverseComparator(comparator)));
   assert(isSorted(A, new ReverseComparator()));
 }


 proc testSomeSorts(input, comparator) {
   var start = input.domain.low;
   var end = input.domain.high;

   var settingsTuple =
     (new MSBRadixSortSettings(CHECK_SORTS=true), // default settings
      new MSBRadixSortSettings(DISTRIBUTE_BUFFER=1,
                               sortSwitch=1,
                               minForTask=1,
                               CHECK_SORTS=true,
                               alwaysSerial=true),
      new MSBRadixSortSettings(DISTRIBUTE_BUFFER=10,
                               sortSwitch=1,
                               minForTask=1,
                               CHECK_SORTS=true,
                               alwaysSerial=false));

   writeln(comparator.type:string);
   writef("Sorting    %ht\n", input);

   var A = input;
   shellSort(A, comparator);
   writef("shellSort  %ht\n", A);
   testSorted(A, comparator);

   var Ar = input;
   shellSort(Ar, new ReverseComparator(comparator));
   writef("shellSort- %ht\n", Ar);
   testReverseSorted(Ar, comparator);

   for param i in 1..settingsTuple.size {
     var s = settingsTuple(i);
     var B = input;
     msbRadixSort(start, end, B, comparator, 0, s);
     if i == 1 then
       writef("radixSort  %ht\n", B);
     testSorted(B, comparator);

     var Br = input;
     msbRadixSort(start, end, Br, new ReverseComparator(comparator), 0, s);
     if i == 1 then
       writef("radixSort- %ht\n", Br);
     testReverseSorted(Br, comparator);
   }
 }

 proc testSortsPositive(input) {
   testSomeSorts(input, new intCriterion());
   testSomeSorts(input, new uintCriterion8());
   testSomeSorts(input, new uintCriterion16());
   testSomeSorts(input, new uintCriterion32());
   testSomeSorts(input, new uintCriterion64());
 }

proc testSortsUnsigned(input) {
   testSomeSorts(input, new uintCriterion8());
   testSomeSorts(input, new uintCriterion16());
   testSomeSorts(input, new uintCriterion32());
   testSomeSorts(input, new uintCriterion64());
 }


 proc testSortsSigned(input) {
   testSomeSorts(input, new intCriterion());
 }

 proc testIntTupleSorts(input) {
   testSomeSorts(input, new intTupleCriterion());
 }

 proc testStringSorts(input) {
   testSomeSorts(input, new stringCriterion());
 }


 proc main() {
  
   assert(chpl_compare(9,3, new uintCriterion8()) > 0);
   assert(chpl_compare(2,3, new uintCriterion8()) < 0);
   assert(chpl_compare(0,0, new uintCriterion8()) == 0);
   assert(chpl_compare(9,3, new ReverseComparator(new uintCriterion8())) < 0);
   assert(chpl_compare(2,3, new ReverseComparator(new uintCriterion8())) > 0);
   assert(chpl_compare(0,0, new ReverseComparator(new uintCriterion8())) == 0);

   testSortsPositive([2,1]);

   testSortsPositive([9,3,8,100,43,2]);
   testSortsPositive([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
   testSortsPositive([1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);

   {
     var A:[1..64] uint;
     for i in 0..63 {
       A[64-i] = 1:uint << i; 
     }
     testSortsUnsigned(A);
   }

   testSortsSigned([-1, 57, 23, -7]);
   testSortsSigned([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
   testSortsSigned([1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);
   testSortsSigned([-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1]);
   testIntTupleSorts([ (4,8), (-7, -20),
                       (0,0), (0,0), (-3, 5), (-3, -5), (4, -8) ]);


   testStringSorts([ "hi", "hii", "hiii", "a", "b", "boo", "", "x", "zoo" ]);

   var array:[1..size] int; 
   fillRandom(array);
   
   for i in array.domain {
     array[i] = abs(array[i]);
   }

   var t: Timer;
   t.start();

   msbRadixSort(1, size, array, new intCriterion(), 0, new
       MSBRadixSortSettings());

   t.stop();
 
   if printStats {
     writeln("Time: ", t.elapsed());
   }

   t.clear();

   //writeln(array);
   writeln("sorted array: ",isSorted(array));

  }
