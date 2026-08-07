import csv
import sys
import os

csv_file = r'D:\Project\KNX\Tool_flash_nordic\tool_flash_fw_knx\key_knx\keys_export.csv'
xml_file = r'D:\Project\KNX\Tool_flash_nordic\tool_flash_fw_knx\key_knx\keys_export.xls'

xml_header = """<?xml version="1.0"?>
<?mso-application progid="Excel.Sheet"?>
<Workbook xmlns="urn:schemas-microsoft-com:office:spreadsheet"
 xmlns:o="urn:schemas-microsoft-com:office:office"
 xmlns:x="urn:schemas-microsoft-com:office:excel"
 xmlns:ss="urn:schemas-microsoft-com:office:spreadsheet"
 xmlns:html="http://www.w3.org/TR/REC-html40">
 <Worksheet ss:Name="Keys">
  <Table>
   <Column ss:Width="50"/>
   <Column ss:Width="250"/>
   <Column ss:Width="250"/>
"""

xml_footer = """  </Table>
 </Worksheet>
</Workbook>
"""

with open(xml_file, 'w', encoding='utf-8') as f_xml:
    f_xml.write(xml_header)
    with open(csv_file, 'r', encoding='utf-8-sig') as f_csv:
        reader = csv.reader(f_csv)
        for i, row in enumerate(reader):
            f_xml.write('   <Row>\n')
            for cell in row:
                if i == 0:
                    # Header row styling
                    f_xml.write(f'    <Cell><Data ss:Type="String">{cell}</Data></Cell>\n')
                else:
                    f_xml.write(f'    <Cell><Data ss:Type="String">{cell}</Data></Cell>\n')
            f_xml.write('   </Row>\n')
    f_xml.write(xml_footer)

print('Generated keys_export.xls successfully')
