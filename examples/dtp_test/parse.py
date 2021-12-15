def parse_file(filename):
    cols = {}
    with open(filename, 'r') as f:
        lines = f.readlines()
        for i in range(len(lines)):
            words = lines[i].split()
            if len(words) == 5:
                if words[0] == 'BlockID':
                    for word in words:
                        cols[word] = []
                    continue
                else:
                    cols['BlockID'].append(int(words[0]))
                    cols['bct'].append(int(words[1]))
                    cols['BlockSize'].append(int(words[2]))
                    cols['Priority'].append(int(words[3]))
                    cols['Deadline'].append(int(words[4]))
            else:
                continue
    return cols

def basic_stat(cols):
    block_num = 0
    block_num_1 = 0
    block_num_2 = 0
    TOTAL_BLOCK_NUM = 1063
    TOTAL_BLOCK_NUM_1 = 703
    TOTAL_BLOCK_NUM_2 = 360
    for i in range(len(cols['BlockID'])):
        block_num += 1
        if cols['Priority'][i] == 1:
            block_num_1 += 1
        elif cols['Priority'][i] == 2:
            block_num_2 += 1
        else:
            pass
    print(block_num, block_num_1, block_num_2)
    print("arrive rate: ", block_num_1 / TOTAL_BLOCK_NUM_1, block_num_2 / TOTAL_BLOCK_NUM_2)
    return block_num, block_num_1, block_num_2

def bct_stat(cols):
    bct1 = 0
    bct2 = 0
    for i in range(len(cols['BlockID'])):
        if cols['Priority'][i] == 1:
            bct1 += cols['bct'][i]
        elif cols['Priority'][i] == 2:
            bct2 += cols['bct'][i]
        else:
            pass 
        
    print("total bct: ", bct1 + bct2)
    print("total bct of priority 1: ", bct1)
    print("total bct of priority 2: ", bct2)
    return bct1, bct2

def stat(cols):
    block_num, block_num_1, block_num_2 = basic_stat(cols)
    bct1, bct2 = bct_stat(cols)
    print("average bct:", bct1 / block_num_1, bct2 / block_num_2)

if __name__ == "__main__":
    ddl = 200
    b_block_num = 0
    b_block_num_1 = 0 
    b_block_num_2 = 0
    b_bct1 = 0
    b_bct2 = 0
    a_block_num = 0
    a_block_num_1 = 0 
    a_block_num_2 = 0
    a_bct1 = 0
    a_bct2 = 0
    for i in range(2,10):
        filename = "client_tos_{ddl}_test_{no}.log".format(ddl=ddl, no=i+1)
        old_filename = "client_fixed_tos_{ddl}_test_{no}.log".format(ddl=ddl, no=i+1)
        print("-------- before tos -------", filename)
        cols = parse_file(filename)
        tblock_num, tblock_num_1, tblock_num_2 = basic_stat(cols)
        b_block_num += tblock_num
        b_block_num_1 += tblock_num_1
        b_block_num_2 += tblock_num_2
        tbct1, tbct2 = bct_stat(cols)
        b_bct1 += tbct1
        b_bct2 += tbct2
        print("-------- after tos -------", old_filename)
        cols = parse_file(old_filename)
        tblock_num, tblock_num_1, tblock_num_2 = basic_stat(cols)
        a_block_num += tblock_num
        a_block_num_1 += tblock_num_1
        a_block_num_2 += tblock_num_2
        tbct1, tbct2 = bct_stat(cols)
        a_bct1 += tbct1
        a_bct2 += tbct2
        print("===========================")
    print("average bct before: ", b_bct1 / b_block_num_1, b_bct2 / b_block_num_2)
    print("average bct after: ", a_bct1 / a_block_num_1, a_bct2 / a_block_num_2)
    # bct_stat(cols)
    # for i in range(10):
    #     old_filename = "client_{ddl}_test_{no}.log".format(ddl=ddl, no=i+1)
    #     print("-------- before tos -------", old_filename)
    #     cols = parse_file(old_filename)
    #     # bct_stat(cols)
    #     basic_stat(cols)
